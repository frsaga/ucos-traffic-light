/*
**********************************************************
* 题目八：基于 uC/OS-II 的多任务电梯调度系统仿真
* 编译环境：Keil MDK 4.x  器件：STM32F103C8（软件仿真）
* 说明：模拟5层楼电梯，用消息队列传递楼层请求，
*       调度任务实现最短路径优先算法，信号量同步
**********************************************************
*/

#include <stdio.h>
#include <stdlib.h>
#include "includes.h"

extern void SER_Init(void);
extern void TIM_Init(void);

/*----------------------------------------------------------
 * 任务配置
 *---------------------------------------------------------*/
#define TASK_STK_SIZE   256u
#define MAX_FLOORS      5u      /* 楼层数 */
#define REQ_Q_SIZE      10u     /* 请求队列深度 */

#define PRIO_SCHEDULER  5u      /* 调度任务   */
#define PRIO_ELEVATOR   6u      /* 电梯运行任务*/
#define PRIO_REQUEST    7u      /* 请求生成任务*/
#define PRIO_DISPLAY    8u      /* 显示任务    */

static OS_STK SchedulerStk[TASK_STK_SIZE];
static OS_STK ElevatorStk [TASK_STK_SIZE];
static OS_STK RequestStk  [TASK_STK_SIZE];
static OS_STK DisplayStk  [TASK_STK_SIZE];

/*----------------------------------------------------------
 * IPC 对象
 *---------------------------------------------------------*/
static OS_EVENT *ReqQueue;          /* 请求消息队列     */
static void     *ReqQBuf[REQ_Q_SIZE];
static OS_EVENT *TargetMbox;        /* 调度→电梯：目标楼层 */
static OS_EVENT *ArrivedSem;        /* 电梯→调度：到达信号 */

/*----------------------------------------------------------
 * 电梯状态
 *---------------------------------------------------------*/
typedef enum {
    ELEV_IDLE  = 0,
    ELEV_UP    = 1,
    ELEV_DOWN  = 2
} ElevState_t;

typedef struct {
    INT8U floor;        /* 请求楼层 */
    INT8U type;         /* 0=内部按钮，1=外部上行，2=外部下行 */
} Request_t;

static Request_t ReqPool[REQ_Q_SIZE];   /* 请求对象池 */
static INT8U     ReqPoolIdx = 0u;

volatile INT8U       g_cur_floor  = 1u;         /* 当前楼层（Watch窗口观察）*/
volatile INT8U       g_target_floor = 1u;        /* 目标楼层 */
volatile ElevState_t g_state      = ELEV_IDLE;
volatile INT32U      g_total_trips = 0u;         /* 总运行次数 */
volatile INT32U      g_total_floors = 0u;        /* 总经过楼层数 */

/* 等待队列：记录各楼层是否有请求 */
static INT8U g_pending[MAX_FLOORS + 1u];        /* 1-indexed */

static const char *StateStr[] = { "IDLE", "UP  ", "DOWN" };

/*----------------------------------------------------------
 * 最短路径调度算法：在待处理请求中找离当前最近的楼层
 *---------------------------------------------------------*/
static INT8U FindNearest(void)
{
    INT8U i;
    INT8U best  = 0u;
    INT8U min_d = 255u;
    INT8U d;

    for (i = 1u; i <= MAX_FLOORS; i++)
    {
        if (g_pending[i])
        {
            d = (g_cur_floor > i) ? (g_cur_floor - i) : (i - g_cur_floor);
            if (d < min_d)
            {
                min_d = d;
                best  = i;
            }
        }
    }
    return best;   /* 0 = 无请求 */
}

/*----------------------------------------------------------
 * 调度任务：从队列取请求，用最短路径算法决定目标楼层
 *---------------------------------------------------------*/
static void TaskScheduler(void *p_arg)
{
    INT8U      err;
    Request_t *preq;
    INT8U      target;
    (void)p_arg;

    while (1)
    {
        /* 从请求队列取出一个请求（非阻塞，有则处理） */
        preq = (Request_t *)OSQAccept(ReqQueue, &err);
        if (preq != NULL)
        {
            if (preq->floor >= 1u && preq->floor <= MAX_FLOORS)
            {
                g_pending[preq->floor] = 1u;
                printf("[SCHED] New request: floor %d (type=%d)\r\n",
                       (int)preq->floor, (int)preq->type);
            }
        }

        /* 如果电梯空闲，派发最近目标 */
        if (g_state == ELEV_IDLE)
        {
            target = FindNearest();
            if (target > 0u)
            {
                g_pending[target] = 0u;
                g_target_floor    = target;
                OSMboxPost(TargetMbox, (void *)&g_target_floor);
            }
        }

        OSTimeDlyHMSM(0u, 0u, 0u, 300u);
    }
}

/*----------------------------------------------------------
 * 电梯运行任务：收到目标楼层后逐层移动，到达后发信号
 *---------------------------------------------------------*/
static void TaskElevator(void *p_arg)
{
    INT8U  err;
    INT8U *ptarget;
    INT8U  target;
    (void)p_arg;

    while (1)
    {
        /* 等待调度任务派发目标楼层 */
        ptarget = (INT8U *)OSMboxPend(TargetMbox, 0u, &err);
        if (err == OS_ERR_NONE)
        {
            target = *ptarget;
            g_total_trips++;

            /* 逐层移动（每层耗时 1s 仿真） */
            while (g_cur_floor != target)
            {
                if (g_cur_floor < target)
                {
                    g_state = ELEV_UP;
                    g_cur_floor++;
                }
                else
                {
                    g_state = ELEV_DOWN;
                    g_cur_floor--;
                }
                g_total_floors++;
                printf("[ELEV] Moving %s -> floor %d\r\n",
                       StateStr[(int)g_state], (int)g_cur_floor);
                OSTimeDlyHMSM(0u, 0u, 1u, 0u);     /* 每层1秒 */
            }

            g_state = ELEV_IDLE;
            printf("[ELEV] Arrived at floor %d. Door open.\r\n",
                   (int)g_cur_floor);
            OSTimeDlyHMSM(0u, 0u, 2u, 0u);         /* 开门停留2秒 */
            printf("[ELEV] Door closed.\r\n");

            OSSemPost(ArrivedSem);                  /* 通知调度已到达 */
        }
    }
}

/*----------------------------------------------------------
 * 请求生成任务：模拟乘客随机按楼层按钮
 * 按固定序列生成，方便验证调度正确性
 *---------------------------------------------------------*/
static void TaskRequest(void *p_arg)
{
    /* 预定义请求序列：{楼层, 类型} */
    static const INT8U seq[][2] = {
        {3u, 0u}, {5u, 1u}, {2u, 0u}, {4u, 2u},
        {1u, 0u}, {5u, 0u}, {3u, 1u}, {2u, 2u}
    };
    INT8U  i   = 0u;
    INT8U  err;
    (void)p_arg;

    OSTimeDlyHMSM(0u, 0u, 2u, 0u);     /* 等待系统稳定后开始 */

    while (1)
    {
        ReqPool[ReqPoolIdx].floor = seq[i][0];
        ReqPool[ReqPoolIdx].type  = seq[i][1];

        err = OSQPost(ReqQueue, (void *)&ReqPool[ReqPoolIdx]);
        if (err == OS_ERR_NONE)
        {
            printf("[BTN ] Button pressed: floor %d type=%d\r\n",
                   (int)seq[i][0], (int)seq[i][1]);
            ReqPoolIdx = (INT8U)((ReqPoolIdx + 1u) % REQ_Q_SIZE);
        }

        i = (INT8U)((i + 1u) % (sizeof(seq) / sizeof(seq[0])));
        OSTimeDlyHMSM(0u, 0u, 5u, 0u);     /* 每5秒产生一个新请求 */
    }
}

/*----------------------------------------------------------
 * 显示任务：每 3s 打印电梯状态总览
 *---------------------------------------------------------*/
static void TaskDisplay(void *p_arg)
{
    INT8U i;
    (void)p_arg;

    while (1)
    {
        printf("\r\n====== ELEVATOR STATUS ======\r\n");
        printf("  Current : floor %d  state=%s\r\n",
               (int)g_cur_floor, StateStr[(int)g_state]);
        printf("  Target  : floor %d\r\n", (int)g_target_floor);
        printf("  Trips   : %lu   Floors traveled: %lu\r\n",
               g_total_trips, g_total_floors);
        printf("  Pending : ");
        for (i = 1u; i <= MAX_FLOORS; i++)
        {
            if (g_pending[i])
                printf("F%d ", (int)i);
        }
        printf("\r\n=============================\r\n\r\n");
        OSTimeDlyHMSM(0u, 0u, 3u, 0u);
    }
}

/*----------------------------------------------------------
 * main
 *---------------------------------------------------------*/
int main(void)
{
    INT8U i;
    INT8U err;

    SER_Init();
    TIM_Init();
    OSInit();

    /* 初始化等待队列 */
    for (i = 0u; i <= MAX_FLOORS; i++)
        g_pending[i] = 0u;

    ReqQueue   = OSQCreate(ReqQBuf, REQ_Q_SIZE);
    TargetMbox = OSMboxCreate((void *)0);
    ArrivedSem = OSSemCreate(0u);

    OSTaskCreate(TaskScheduler, (void *)0, &SchedulerStk[TASK_STK_SIZE-1u], PRIO_SCHEDULER);
    OSTaskCreate(TaskElevator,  (void *)0, &ElevatorStk [TASK_STK_SIZE-1u], PRIO_ELEVATOR);
    OSTaskCreate(TaskRequest,   (void *)0, &RequestStk  [TASK_STK_SIZE-1u], PRIO_REQUEST);
    OSTaskCreate(TaskDisplay,   (void *)0, &DisplayStk  [TASK_STK_SIZE-1u], PRIO_DISPLAY);

    OSStart();

    return 0;
}
