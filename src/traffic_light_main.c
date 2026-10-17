/*
**********************************************************
* 题目七：基于 uC/OS-II 的多任务交通灯控制系统
* 编译环境：Keil MDK 4.x  器件：STM32F103C8（软件仿真）
* 说明：用任务状态机模拟十字路口交通灯时序，
*       支持紧急车辆抢占模式，消息邮箱传递控制指令
**********************************************************
*/

#include <stdio.h>
#include "includes.h"

extern void SER_Init(void);
extern void TIM_Init(void);

/*----------------------------------------------------------
 * 任务配置
 *---------------------------------------------------------*/
#define TASK_STK_SIZE   256u

#define PRIO_CTRL       5u      /* 主控任务（状态机） */
#define PRIO_NS         6u      /* 南北方向灯任务     */
#define PRIO_EW         7u      /* 东西方向灯任务     */
#define PRIO_EMERGENCY  4u      /* 紧急抢占任务（最高）*/
#define PRIO_DISPLAY    8u      /* 状态显示任务       */

static OS_STK CtrlStk     [TASK_STK_SIZE];
static OS_STK NSStk       [TASK_STK_SIZE];
static OS_STK EWStk       [TASK_STK_SIZE];
static OS_STK EmergencyStk[TASK_STK_SIZE];
static OS_STK DisplayStk  [TASK_STK_SIZE];

/*----------------------------------------------------------
 * 交通灯状态定义
 *---------------------------------------------------------*/
typedef enum {
    PHASE_NS_GREEN  = 0,    /* 南北绿灯，东西红灯 */
    PHASE_NS_YELLOW = 1,    /* 南北黄灯，东西红灯 */
    PHASE_EW_GREEN  = 2,    /* 东西绿灯，南北红灯 */
    PHASE_EW_YELLOW = 3,    /* 东西黄灯，南北红灯 */
    PHASE_EMERGENCY = 4     /* 紧急模式：全红闪烁 */
} TrafficPhase_t;

typedef enum {
    LIGHT_RED    = 0,
    LIGHT_YELLOW = 1,
    LIGHT_GREEN  = 2
} LightColor_t;

/* 相位持续时间与显示参数 */
#define TICK_PER_SEC        100u
#define GREEN_BASE_SEC       30u
#define GREEN_SHORT_SEC      25u
#define GREEN_LONG_SEC       35u
#define GREEN_MAX_SEC        40u
#define YELLOW_SEC            5u
#define EMERGENCY_SEC         6u
#define PROGRESS_BAR_LEN     20u

/*----------------------------------------------------------
 * IPC 对象
 *---------------------------------------------------------*/
static OS_EVENT *PhaseMbox;      /* 主控→NS/EW：当前相位 */
static OS_EVENT *EmergencySem;   /* 紧急任务→主控：触发紧急模式 */
static OS_EVENT *EmergencyEndSem;/* 紧急任务→主控：结束紧急模式 */

/*----------------------------------------------------------
 * 全局状态（Watch 窗口观察）
 *---------------------------------------------------------*/
volatile TrafficPhase_t g_phase       = PHASE_NS_GREEN;
volatile LightColor_t   g_ns_light    = LIGHT_GREEN;
volatile LightColor_t   g_ew_light    = LIGHT_RED;
volatile INT32U         g_phase_cnt   = 0u;  /* 相位切换次数 */
volatile INT32U         g_emergency_cnt = 0u;/* 紧急模式触发次数 */
volatile INT8U          g_emergency_active = 0u;
volatile INT16U         g_ns_car_cnt = 8u;   /* 南北方向模拟车流量 */
volatile INT16U         g_ew_car_cnt = 3u;   /* 东西方向模拟车流量 */
volatile INT16U         g_ns_green_sec = GREEN_LONG_SEC;
volatile INT16U         g_ew_green_sec = GREEN_SHORT_SEC;
volatile INT16U         g_phase_elapsed_sec = 0u;
volatile INT16U         g_phase_total_sec = GREEN_LONG_SEC;
volatile const char    *g_last_adjust_reason = "NS heavy traffic";

static const char *LightStr[] = {"RED  ", "YELLOW", "GREEN"};
static const char *PhaseStr[] = {
    "NS-GREEN / EW-RED  ",
    "NS-YELLOW/ EW-RED  ",
    "NS-RED   / EW-GREEN",
    "NS-RED   / EW-YELLOW",
    "EMERGENCY ALL-RED  "
};

typedef struct {
    INT16U ns_cars;
    INT16U ew_cars;
} TrafficFlowSample_t;

static const TrafficFlowSample_t TrafficFlowSeq[] = {
    {8u,  3u},
    {4u,  9u},
    {5u,  5u},
    {10u, 2u}
};

#define TRAFFIC_FLOW_SEQ_SIZE \
    (sizeof(TrafficFlowSeq) / sizeof(TrafficFlowSeq[0]))

static INT8U g_flow_index = 0u;
static INT8U g_need_flow_update = 1u;

/*----------------------------------------------------------
 * 动态配时与进度显示辅助函数
 *---------------------------------------------------------*/
static INT16U CalcGreenSec(INT16U own_cars, INT16U other_cars)
{
    INT16U sec = GREEN_BASE_SEC;

    if (own_cars >= (INT16U)(other_cars + 7u)) {
        sec = GREEN_MAX_SEC;
    } else if (own_cars >= (INT16U)(other_cars + 4u)) {
        sec = GREEN_LONG_SEC;
    } else if ((INT16U)(own_cars + 4u) <= other_cars) {
        sec = GREEN_SHORT_SEC;
    }

    return sec;
}

static void UpdateTrafficFlow(void)
{
    g_ns_car_cnt = TrafficFlowSeq[g_flow_index].ns_cars;
    g_ew_car_cnt = TrafficFlowSeq[g_flow_index].ew_cars;
    g_flow_index++;
    if (g_flow_index >= TRAFFIC_FLOW_SEQ_SIZE) {
        g_flow_index = 0u;
    }
}

static void AdjustGreenTime(void)
{
    g_ns_green_sec = CalcGreenSec(g_ns_car_cnt, g_ew_car_cnt);
    g_ew_green_sec = CalcGreenSec(g_ew_car_cnt, g_ns_car_cnt);

    if (g_ns_green_sec > g_ew_green_sec) {
        g_last_adjust_reason = "NS traffic is heavier";
    } else if (g_ew_green_sec > g_ns_green_sec) {
        g_last_adjust_reason = "EW traffic is heavier";
    } else {
        g_last_adjust_reason = "Balanced traffic";
    }

    printf("\r\n[ADAPT] Cars NS=%u EW=%u, GreenPlan NS=%us EW=%us (%s)\r\n\r\n",
           g_ns_car_cnt, g_ew_car_cnt,
           g_ns_green_sec, g_ew_green_sec,
           g_last_adjust_reason);
}

static void PublishPhase(TrafficPhase_t phase, LightColor_t ns_light,
                         LightColor_t ew_light, INT16U total_sec)
{
    g_phase = phase;
    g_ns_light = ns_light;
    g_ew_light = ew_light;
    g_phase_elapsed_sec = 0u;
    g_phase_total_sec = total_sec;
    g_phase_cnt++;
    PhaseMbox->OSEventPtr = (void *)&g_phase;
    OSMboxPost(PhaseMbox, (void *)&g_phase);
}

static INT8U RunPhaseWithProgress(INT16U total_sec)
{
    INT16U elapsed;

    for (elapsed = 0u; elapsed < total_sec; elapsed++) {
        OSTimeDly(TICK_PER_SEC);
        g_phase_elapsed_sec = (INT16U)(elapsed + 1u);

        if (OSSemAccept(EmergencySem) > 0u) {
            g_emergency_active = 1u;
            g_emergency_cnt++;
            return 1u;
        }
    }

    return 0u;
}

static void PrintProgressLine(const char *name, INT16U elapsed, INT16U total)
{
    INT16U filled;
    INT16U i;

    if (total == 0u) {
        total = 1u;
    }

    if (elapsed > total) {
        elapsed = total;
    }

    filled = (INT16U)(((INT32U)elapsed * PROGRESS_BAR_LEN) / total);
    printf("  %-9s [", name);
    for (i = 0u; i < PROGRESS_BAR_LEN; i++) {
        printf("%c", (i < filled) ? '#' : '-');
    }
    printf("] %u/%us\r\n", elapsed, total);
}

static void PrintLockedLine(const char *name)
{
    INT16U i;

    printf("  %-9s [", name);
    for (i = 0u; i < PROGRESS_BAR_LEN; i++) {
        printf("#");
    }
    printf("] locked\r\n");
}

static void PrintSolidLine(const char *name)
{
    INT16U i;

    printf("  %-9s [", name);
    for (i = 0u; i < PROGRESS_BAR_LEN; i++) {
        printf("#");
    }
    printf("]\r\n");
}

static void PrintPhaseProgress(void)
{
    switch (g_phase) {
        case PHASE_NS_GREEN:
            PrintProgressLine("NS_GREEN", g_phase_elapsed_sec,
                              g_phase_total_sec);
            PrintLockedLine("EW_RED");
            break;

        case PHASE_NS_YELLOW:
            PrintProgressLine("NS_YELLOW", g_phase_elapsed_sec,
                              g_phase_total_sec);
            PrintLockedLine("EW_RED");
            break;

        case PHASE_EW_GREEN:
            PrintProgressLine("EW_GREEN", g_phase_elapsed_sec,
                              g_phase_total_sec);
            PrintLockedLine("NS_RED");
            break;

        case PHASE_EW_YELLOW:
            PrintProgressLine("EW_YELLOW", g_phase_elapsed_sec,
                              g_phase_total_sec);
            PrintLockedLine("NS_RED");
            break;

        case PHASE_EMERGENCY:
            PrintProgressLine("EMERGENCY", g_phase_elapsed_sec,
                              g_phase_total_sec);
            PrintSolidLine("ALL_RED");
            break;

        default:
            PrintProgressLine("UNKNOWN", g_phase_elapsed_sec,
                              g_phase_total_sec);
            break;
    }
}

/*----------------------------------------------------------
 * 主控任务：状态机驱动相位切换
 *---------------------------------------------------------*/
static void TaskCtrl(void *p_arg)
{
    TrafficPhase_t   phase = PHASE_NS_GREEN;
    (void)p_arg;

    while (1)
    {
        if (OSSemAccept(EmergencySem) > 0u) {
            phase = PHASE_EMERGENCY;
            g_emergency_active = 1u;
            g_emergency_cnt++;
        }

        switch (phase)
        {
            case PHASE_NS_GREEN:
                if (g_need_flow_update != 0u) {
                    UpdateTrafficFlow();
                    AdjustGreenTime();
                    g_need_flow_update = 0u;
                }
                PublishPhase(PHASE_NS_GREEN, LIGHT_GREEN, LIGHT_RED,
                             g_ns_green_sec);
                if (RunPhaseWithProgress(g_ns_green_sec) != 0u) {
                    phase = PHASE_EMERGENCY;
                } else {
                    phase = PHASE_NS_YELLOW;
                }
                break;

            case PHASE_NS_YELLOW:
                PublishPhase(PHASE_NS_YELLOW, LIGHT_YELLOW, LIGHT_RED,
                             YELLOW_SEC);
                if (RunPhaseWithProgress(YELLOW_SEC) != 0u) {
                    phase = PHASE_EMERGENCY;
                } else {
                    phase = PHASE_EW_GREEN;
                }
                break;

            case PHASE_EW_GREEN:
                PublishPhase(PHASE_EW_GREEN, LIGHT_RED, LIGHT_GREEN,
                             g_ew_green_sec);
                if (RunPhaseWithProgress(g_ew_green_sec) != 0u) {
                    phase = PHASE_EMERGENCY;
                } else {
                    phase = PHASE_EW_YELLOW;
                }
                break;

            case PHASE_EW_YELLOW:
                PublishPhase(PHASE_EW_YELLOW, LIGHT_RED, LIGHT_YELLOW,
                             YELLOW_SEC);
                if (RunPhaseWithProgress(YELLOW_SEC) != 0u) {
                    phase = PHASE_EMERGENCY;
                } else {
                    g_need_flow_update = 1u;
                    phase = PHASE_NS_GREEN;
                }
                break;

            case PHASE_EMERGENCY:
                PublishPhase(PHASE_EMERGENCY, LIGHT_RED, LIGHT_RED,
                             EMERGENCY_SEC);
                RunPhaseWithProgress(EMERGENCY_SEC);
                OSSemAccept(EmergencyEndSem);
                g_emergency_active = 0u;
                phase = PHASE_NS_GREEN;   /* 恢复正常 */
                break;

            default:
                phase = PHASE_NS_GREEN;
                break;
        }
    }
}

/*----------------------------------------------------------
 * 南北方向灯任务：接收相位，输出灯状态
 *---------------------------------------------------------*/
static void TaskNS(void *p_arg)
{
    INT8U            err;
    TrafficPhase_t  *p_phase;
    (void)p_arg;

    while (1)
    {
        p_phase = (TrafficPhase_t *)OSMboxPend(PhaseMbox, 0u, &err);
        if (err == OS_ERR_NONE)
        {
            printf("[NS ] Phase = %s  Light = %s\r\n",
                   PhaseStr[(int)*p_phase], LightStr[(int)g_ns_light]);
        }
    }
}

/*----------------------------------------------------------
 * 东西方向灯任务：同步处理灯状态
 *---------------------------------------------------------*/
static void TaskEW(void *p_arg)
{
    (void)p_arg;
    while (1)
    {
        printf("[EW ] Light = %s\r\n", LightStr[(int)g_ew_light]);
        OSTimeDlyHMSM(0u, 0u, 1u, 0u);     /* 每秒刷新一次 */
    }
}

/*----------------------------------------------------------
 * 紧急任务：每 20s 模拟触发一次紧急模式，持续 6s 后解除
 *---------------------------------------------------------*/
static void TaskEmergency(void *p_arg)
{
    (void)p_arg;
    OSTimeDlyHMSM(0u, 0u, 20u, 0u);    /* 等待 20s 后首次触发 */

    while (1)
    {
        printf("\r\n[!!!] EMERGENCY VEHICLE DETECTED! All red.\r\n\r\n");
        OSSemPost(EmergencySem);            /* 触发紧急模式 */
        OSTimeDlyHMSM(0u, 0u, 6u, 0u);     /* 紧急模式持续 6s */
        OSSemPost(EmergencyEndSem);         /* 解除紧急模式 */
        printf("\r\n[!!!] Emergency cleared. Resuming normal.\r\n\r\n");
        OSTimeDlyHMSM(0u, 0u, 30u, 0u);    /* 30s 后再次触发 */
    }
}

/*----------------------------------------------------------
 * 显示任务：每 2s 打印路口整体状态
 *---------------------------------------------------------*/
static void TaskDisplay(void *p_arg)
{
    (void)p_arg;
    while (1)
    {
        printf("\r\n--- Intersection Status ---\r\n");
        printf("  Phase     : %s\r\n", PhaseStr[(int)g_phase]);
        printf("  NS Light  : %s\r\n", LightStr[(int)g_ns_light]);
        printf("  EW Light  : %s\r\n", LightStr[(int)g_ew_light]);
        printf("  Cars      : NS=%u  EW=%u\r\n",
               g_ns_car_cnt, g_ew_car_cnt);
        printf("  GreenPlan : NS=%us EW=%us\r\n",
               g_ns_green_sec, g_ew_green_sec);
        printf("  Adjust    : %s\r\n", g_last_adjust_reason);
        printf("  PhaseSwitch: %lu  Emergency: %lu  Active: %s\r\n",
               g_phase_cnt, g_emergency_cnt,
               g_emergency_active ? "YES" : "NO");
        printf("\r\nPhase Progress:\r\n");
        PrintPhaseProgress();
        printf("---------------------------\r\n\r\n");
        OSTimeDlyHMSM(0u, 0u, 2u, 0u);
    }
}

/*----------------------------------------------------------
 * main
 *---------------------------------------------------------*/
int main(void)
{
    SER_Init();
    TIM_Init();
    OSInit();

    PhaseMbox       = OSMboxCreate((void *)0);
    EmergencySem    = OSSemCreate(0u);
    EmergencyEndSem = OSSemCreate(0u);

    OSTaskCreate(TaskEmergency, (void *)0, &EmergencyStk[TASK_STK_SIZE-1u], PRIO_EMERGENCY);
    OSTaskCreate(TaskCtrl,      (void *)0, &CtrlStk     [TASK_STK_SIZE-1u], PRIO_CTRL);
    OSTaskCreate(TaskNS,        (void *)0, &NSStk       [TASK_STK_SIZE-1u], PRIO_NS);
    OSTaskCreate(TaskEW,        (void *)0, &EWStk       [TASK_STK_SIZE-1u], PRIO_EW);
    OSTaskCreate(TaskDisplay,   (void *)0, &DisplayStk  [TASK_STK_SIZE-1u], PRIO_DISPLAY);

    OSStart();

    return 0;
}
