/*
**********************************************************
* 题目三：基于 uC/OS-II 的 PID 电机调速系统仿真
* 编译环境：Keil MDK 4.x  器件：STM32F103C8（软件仿真）
* 说明：用软件定时器（OS_TMR）每 10ms 触发 PID 运算
*       用事件标志组同步采样就绪信号
*       在 Keil Watch 窗口观察 g_speed_act 收敛过程
**********************************************************
*/

#include <stdio.h>
#include "includes.h"

extern void SER_Init(void);   /* Serial.c  */
extern void TIM_Init(void);   /* Time.c    */
#include <math.h>



/*----------------------------------------------------------
 * 任务配置
 *---------------------------------------------------------*/
#define TASK_STK_SIZE   256u

#define PRIO_PID        5u
#define PRIO_DISPLAY    7u

static OS_STK PidStk    [TASK_STK_SIZE];
static OS_STK DisplayStk[TASK_STK_SIZE];

/*----------------------------------------------------------
 * IPC 对象
 *---------------------------------------------------------*/
static OS_FLAG_GRP *SpeedFlags;     /* 事件标志组 */
static OS_TMR      *PidTimer;       /* 软件定时器 */

#define FLAG_SPEED_READY    0x01u   /* bit0：速度采样就绪 */

/*----------------------------------------------------------
 * 全局仿真变量（可在 Keil Watch 窗口直接观察）
 *---------------------------------------------------------*/
volatile float g_speed_ref = 1000.0f;   /* 目标转速 rpm */
volatile float g_speed_act = 0.0f;      /* 实际转速（仿真一阶惯性） */
volatile float g_pwm       = 0.0f;      /* PID 输出（即控制量） */

/*----------------------------------------------------------
 * PID 结构体与计算
 *---------------------------------------------------------*/
typedef struct
{
    float Kp;
    float Ki;
    float Kd;
    float prev_err;
    float integral;
    float out_max;      /* 输出限幅 */
    float out_min;
} PID_t;

/* 默认参数，可在 Watch 窗口运行时修改 Kp/Ki/Kd 调参 */
static PID_t g_pid = {0.8f, 0.05f, 0.2f, 0.0f, 0.0f, 2000.0f, -2000.0f};

static float PID_Calc(PID_t *p, float ref, float act)
{
    float err = ref - act;
    float out;

    p->integral += err;

    /* 积分限幅，防止积分饱和 */
    if (p->integral >  500.0f) p->integral =  500.0f;
    if (p->integral < -500.0f) p->integral = -500.0f;

    out = p->Kp * err
        + p->Ki * p->integral
        + p->Kd * (err - p->prev_err);

    p->prev_err = err;

    /* 输出限幅 */
    if (out > p->out_max) out = p->out_max;
    if (out < p->out_min) out = p->out_min;

    return out;
}

/*----------------------------------------------------------
 * 软件定时器回调：每 10ms 触发
 * 1. 更新一阶惯性模型（模拟电机动态）
 * 2. 置位事件标志，通知 PID 任务运算
 *---------------------------------------------------------*/
static void PidTimerCB(OS_TMR *p_tmr, void *p_arg)
{
    INT8U err;
    (void)p_tmr;
    (void)p_arg;

    /* 一阶惯性模型：tau=0.1，模拟电机响应 */
    g_speed_act += (g_pwm - g_speed_act) * 0.08f;

    /* 通知 PID 任务：速度采样已就绪 */
    OSFlagPost(SpeedFlags,
               (OS_FLAGS)FLAG_SPEED_READY,
               OS_FLAG_SET,
               &err);
}

/*----------------------------------------------------------
 * PID 任务：等待采样就绪标志，执行 PID 运算
 *---------------------------------------------------------*/
static void TaskPid(void *p_arg)
{
    INT8U err;
    (void)p_arg;

    while (1)
    {
        /* 等待 bit0 置位，自动清除 */
        OSFlagPend(SpeedFlags,
                   (OS_FLAGS)FLAG_SPEED_READY,
                   OS_FLAG_WAIT_SET_ALL + OS_FLAG_CONSUME,
                   0u,
                   &err);

        if (err == OS_ERR_NONE)
        {
            g_pwm = PID_Calc(&g_pid,
                             (float)g_speed_ref,
                             (float)g_speed_act);
        }
    }
}

/* 显示任务：每 500ms 打印一次状态 */
static void TaskDisplay(void *p_arg)
{
    (void)p_arg;
    while (1)
    {
        printf("[PID] Ref=%.1f  Act=%.1f  PWM=%.2f  Kp=%.2f Ki=%.3f Kd=%.2f\r\n",
               g_speed_ref, g_speed_act, g_pwm,
               g_pid.Kp, g_pid.Ki, g_pid.Kd);
        OSTimeDlyHMSM(0u, 0u, 0u, 500u);
    }
}

/*----------------------------------------------------------
 * main
 *---------------------------------------------------------*/
int main(void)
{
    INT8U err;

    SER_Init();                   /* 初始化串口（printf需要） */
    TIM_Init();                    /* 初始化系统时钟 tick      */
    OSInit();

    /* 创建事件标志组 */
    SpeedFlags = OSFlagCreate(0u, &err);

    /* 创建软件定时器：1 tick 后启动，每 1 tick 触发一次
     * （OS_TMR_CFG_TICKS_PER_SEC 决定定时器频率，
     *   通常配置为 100Hz，即 10ms/tick） */
    PidTimer = OSTmrCreate(1u,              /* 初始等待 */
                           1u,              /* 周期（tick） */
                           OS_TMR_OPT_PERIODIC,
                           PidTimerCB,
                           (void *)0,
                           (INT8U *)"PidTmr",
                           &err);

    OSTmrStart(PidTimer, &err);

    OSTaskCreate(TaskPid,
                 (void *)0,
                 &PidStk[TASK_STK_SIZE - 1u],
                 PRIO_PID);

    OSTaskCreate(TaskDisplay,
                 (void *)0,
                 &DisplayStk[TASK_STK_SIZE - 1u],
                 PRIO_DISPLAY);

    OSStart();

    return 0;
}
