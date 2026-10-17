/*
**********************************************************
* 题目六：基于 uC/OS-II 的生产者-消费者缓冲区管理系统
* 编译环境：Keil MDK 4.x  器件：STM32F103C8（软件仿真）
* 说明：经典生产者-消费者问题，用计数信号量控制
*       缓冲区满/空状态，互斥量保护临界区
**********************************************************
*/

#include <stdio.h>
#include "includes.h"

extern void SER_Init(void);   /* Serial.c  */
extern void TIM_Init(void);   /* Time.c    */

/*----------------------------------------------------------
 * 任务配置
 *---------------------------------------------------------*/
#define TASK_STK_SIZE   256u
#define BUFFER_SIZE     8u      /* 环形缓冲区容量 */

#define PRIO_PRODUCER1  5u
#define PRIO_PRODUCER2  6u
#define PRIO_CONSUMER1  7u
#define PRIO_CONSUMER2  8u
#define PRIO_MONITOR    9u

static OS_STK Producer1Stk[TASK_STK_SIZE];
static OS_STK Producer2Stk[TASK_STK_SIZE];
static OS_STK Consumer1Stk[TASK_STK_SIZE];
static OS_STK Consumer2Stk[TASK_STK_SIZE];
static OS_STK MonitorStk  [TASK_STK_SIZE];

/*----------------------------------------------------------
 * IPC 对象
 *---------------------------------------------------------*/
static OS_EVENT *EmptySem;   /* 计数信号量：空槽数量，初值=BUFFER_SIZE */
static OS_EVENT *FullSem;    /* 计数信号量：满槽数量，初值=0           */
static OS_EVENT *BufMutex;   /* 互斥量：保护缓冲区读写指针             */

/*----------------------------------------------------------
 * 环形缓冲区
 *---------------------------------------------------------*/
typedef struct {
    INT32U  id;         /* 数据编号     */
    INT32U  value;      /* 数据值       */
    INT8U   producer;   /* 生产者编号   */
} Item_t;

static Item_t  Buffer[BUFFER_SIZE];
static INT8U   BufHead = 0u;    /* 消费指针 */
static INT8U   BufTail = 0u;    /* 生产指针 */

/*----------------------------------------------------------
 * 统计变量
 *---------------------------------------------------------*/
volatile INT32U g_produce1_cnt = 0u;
volatile INT32U g_produce2_cnt = 0u;
volatile INT32U g_consume1_cnt = 0u;
volatile INT32U g_consume2_cnt = 0u;
volatile INT32U g_total_items  = 0u;

/*----------------------------------------------------------
 * 生产者公共函数
 *---------------------------------------------------------*/
static void Produce(INT8U producer_id, INT32U value, INT32U id)
{
    INT8U err;

    OSSemPend(EmptySem, 0u, &err);          /* 等待空槽 */

    OSMutexPend(BufMutex, 0u, &err);        /* 锁定缓冲区 */
    Buffer[BufTail].id       = id;
    Buffer[BufTail].value    = value;
    Buffer[BufTail].producer = producer_id;
    BufTail = (INT8U)((BufTail + 1u) % BUFFER_SIZE);
    g_total_items++;
    OSMutexPost(BufMutex);                  /* 释放缓冲区 */

    OSSemPost(FullSem);                     /* 通知消费者有新数据 */

    printf("[P%d] Produced id=%lu val=%lu  (tail=%d)\r\n",
           (int)producer_id, id, value, (int)BufTail);
}

/*----------------------------------------------------------
 * 消费者公共函数
 *---------------------------------------------------------*/
static void Consume(INT8U consumer_id)
{
    INT8U  err;
    Item_t item;

    OSSemPend(FullSem, 0u, &err);           /* 等待满槽 */

    OSMutexPend(BufMutex, 0u, &err);        /* 锁定缓冲区 */
    item    = Buffer[BufHead];
    BufHead = (INT8U)((BufHead + 1u) % BUFFER_SIZE);
    OSMutexPost(BufMutex);                  /* 释放缓冲区 */

    OSSemPost(EmptySem);                    /* 通知生产者有空槽 */

    printf("[C%d] Consumed id=%lu val=%lu from P%d  (head=%d)\r\n",
           (int)consumer_id, item.id, item.value,
           (int)item.producer, (int)BufHead);
}

/*----------------------------------------------------------
 * 任务函数
 *---------------------------------------------------------*/

/* 生产者1：每 400ms 生产一个数据 */
static void TaskProducer1(void *p_arg)
{
    (void)p_arg;
    while (1)
    {
        g_produce1_cnt++;
        Produce(1u, g_produce1_cnt * 10u, g_produce1_cnt);
        OSTimeDlyHMSM(0u, 0u, 0u, 400u);
    }
}

/* 生产者2：每 600ms 生产一个数据 */
static void TaskProducer2(void *p_arg)
{
    (void)p_arg;
    while (1)
    {
        g_produce2_cnt++;
        Produce(2u, g_produce2_cnt * 20u + 1u, g_produce2_cnt + 1000u);
        OSTimeDlyHMSM(0u, 0u, 0u, 600u);
    }
}

/* 消费者1：每 500ms 消费一个数据 */
static void TaskConsumer1(void *p_arg)
{
    (void)p_arg;
    while (1)
    {
        Consume(1u);
        g_consume1_cnt++;
        OSTimeDlyHMSM(0u, 0u, 0u, 500u);
    }
}

/* 消费者2：每 800ms 消费一个数据 */
static void TaskConsumer2(void *p_arg)
{
    (void)p_arg;
    while (1)
    {
        Consume(2u);
        g_consume2_cnt++;
        OSTimeDlyHMSM(0u, 0u, 0u, 800u);
    }
}

/* 监控任务：每 3s 打印系统状态 */
static void TaskMonitor(void *p_arg)
{
    (void)p_arg;
    while (1)
    {
        printf("\r\n=== BUFFER MONITOR ===\r\n");
        printf("  Buffer   : head=%d  tail=%d  used=%d/%d\r\n",
               (int)BufHead, (int)BufTail,
               (int)((BufTail - BufHead + BUFFER_SIZE) % BUFFER_SIZE),
               (int)BUFFER_SIZE);
        printf("  Produced : P1=%lu  P2=%lu  total=%lu\r\n",
               g_produce1_cnt, g_produce2_cnt, g_total_items);
        printf("  Consumed : C1=%lu  C2=%lu\r\n",
               g_consume1_cnt, g_consume2_cnt);
        printf("  EmptySem : %d  FullSem: %d\r\n",
               (int)OSSemAccept(EmptySem),
               (int)OSSemAccept(FullSem));
        printf("======================\r\n\r\n");
        OSTimeDlyHMSM(0u, 0u, 3u, 0u);
    }
}

/*----------------------------------------------------------
 * main
 *---------------------------------------------------------*/
int main(void)
{
    INT8U err;

    SER_Init();
    TIM_Init();
    OSInit();

    EmptySem = OSSemCreate(BUFFER_SIZE);    /* 初始全空，空槽=8 */
    FullSem  = OSSemCreate(0u);             /* 初始无数据，满槽=0 */
    BufMutex = OSMutexCreate(4u, &err);     /* 互斥量，天花板优先级=4 */

    OSTaskCreate(TaskProducer1, (void *)0, &Producer1Stk[TASK_STK_SIZE-1u], PRIO_PRODUCER1);
    OSTaskCreate(TaskProducer2, (void *)0, &Producer2Stk[TASK_STK_SIZE-1u], PRIO_PRODUCER2);
    OSTaskCreate(TaskConsumer1, (void *)0, &Consumer1Stk[TASK_STK_SIZE-1u], PRIO_CONSUMER1);
    OSTaskCreate(TaskConsumer2, (void *)0, &Consumer2Stk[TASK_STK_SIZE-1u], PRIO_CONSUMER2);
    OSTaskCreate(TaskMonitor,   (void *)0, &MonitorStk  [TASK_STK_SIZE-1u], PRIO_MONITOR);

    OSStart();

    return 0;
}
