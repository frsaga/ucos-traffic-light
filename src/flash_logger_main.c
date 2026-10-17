/*
**********************************************************
* 题目四：基于 uC/OS-II 的嵌入式循环日志系统设计
* 编译环境：Keil MDK 4.x  器件：STM32F103C8（软件仿真）
* 说明：内存分区管理日志块，消息队列传递，
*       循环覆盖写入模拟 Flash（数组），
*       统计内存使用率和队列深度
**********************************************************
*/

#include <stdio.h>
#include "includes.h"

extern void SER_Init(void);   /* Serial.c  */
extern void TIM_Init(void);   /* Time.c    */
#include <string.h>



/*----------------------------------------------------------
 * 常量定义
 *---------------------------------------------------------*/
#define TASK_STK_SIZE   256u

#define PRIO_PRODUCER   5u
#define PRIO_CONSUMER   6u
#define PRIO_STAT       8u      /* 统计任务，低优先级 */

#define LOG_BLOCK_SIZE  64u     /* 每条日志最大字节数 */
#define LOG_POOL_SIZE   20u     /* 内存分区块数量 */
#define FLASH_PAGES     50u     /* 模拟 Flash 页数 */

/*----------------------------------------------------------
 * 任务堆栈
 *---------------------------------------------------------*/
static OS_STK ProducerStk[TASK_STK_SIZE];
static OS_STK ConsumerStk[TASK_STK_SIZE];
static OS_STK StatStk    [TASK_STK_SIZE];

/*----------------------------------------------------------
 * 内存分区、消息队列
 *---------------------------------------------------------*/
static OS_MEM  *LogMemPart;
static INT8U    LogMemBuf[LOG_POOL_SIZE][LOG_BLOCK_SIZE];   /* 内存分区原始空间 */

static OS_EVENT *LogQueue;
static void     *LogQBuf[LOG_POOL_SIZE];

/*----------------------------------------------------------
 * 模拟 Flash 存储
 *---------------------------------------------------------*/
static char  Flash[FLASH_PAGES][LOG_BLOCK_SIZE];
static INT8U FlashHead  = 0u;   /* 循环写指针 */
static INT8U FlashCount = 0u;   /* 已写入条数 */

/*----------------------------------------------------------
 * 统计变量（可在 Keil Watch 窗口观察）
 *---------------------------------------------------------*/
volatile INT32U g_produce_cnt = 0u;   /* 生产总条数 */
volatile INT32U g_consume_cnt = 0u;   /* 消费总条数 */
volatile INT32U g_drop_cnt    = 0u;   /* 丢弃条数（内存不足） */

/*----------------------------------------------------------
 * 生产者任务：每 200ms 生成一条日志
 *---------------------------------------------------------*/
static void TaskProducer(void *p_arg)
{
    INT8U  err;
    char  *blk;
    (void)p_arg;

    while (1)
    {
        /* 从内存分区申请一块 */
        blk = (char *)OSMemGet(LogMemPart, &err);

        if (err == OS_ERR_NONE)
        {
            g_produce_cnt++;
            snprintf(blk, LOG_BLOCK_SIZE,
                     "[LOG#%04lu] tick=%lu  temp=%d",
                     g_produce_cnt,
                     (unsigned long)OSTimeGet(),
                     (int)(20 + g_produce_cnt % 60));   /* 仿真温度值 */

            /* 投入消息队列 */
            err = OSQPost(LogQueue, (void *)blk);
            if (err != OS_ERR_NONE)
            {
                /* 队列满，归还内存块并计数 */
                OSMemPut(LogMemPart, blk);
                g_drop_cnt++;
            }
        }
        else
        {
            /* 内存分区耗尽 */
            g_drop_cnt++;
        }

        OSTimeDlyHMSM(0u, 0u, 0u, 200u);
    }
}

/*----------------------------------------------------------
 * 消费者任务：从队列取出日志，写入模拟 Flash
 *---------------------------------------------------------*/
static void TaskConsumer(void *p_arg)
{
    INT8U  err;
    char  *blk;
    (void)p_arg;

    while (1)
    {
        blk = (char *)OSQPend(LogQueue, 0u, &err);

        if (err == OS_ERR_NONE)
        {
            /* 循环覆盖写入 Flash */
            memcpy(Flash[FlashHead], blk, LOG_BLOCK_SIZE);
            FlashHead = (INT8U)((FlashHead + 1u) % FLASH_PAGES);
            if (FlashCount < FLASH_PAGES) FlashCount++;

            g_consume_cnt++;
            printf("[FLASH@%02d] %s\r\n", (int)FlashHead, blk);

            /* 归还内存块 */
            OSMemPut(LogMemPart, blk);
        }
    }
}

/*----------------------------------------------------------
 * 统计任务：每 2 秒打印内存分区使用率和队列深度
 *---------------------------------------------------------*/
static void TaskStat(void *p_arg)
{
    OS_MEM_DATA mem_data;
    OS_Q_DATA   q_data;
    INT8U       err;
    (void)p_arg;

    while (1)
    {
        OSMemQuery(LogMemPart, &mem_data);
        OSQQuery(LogQueue, &q_data);

        printf("\r\n=== STAT ===\r\n");
        printf("  MemPart  : used=%u / total=%u  (free=%u)\r\n",
               (unsigned)(LOG_POOL_SIZE - mem_data.OSNFree),
               (unsigned)LOG_POOL_SIZE,
               (unsigned)mem_data.OSNFree);
        printf("  Queue    : msgs=%u / size=%u\r\n",
               (unsigned)q_data.OSNMsgs,
               (unsigned)q_data.OSQSize);
        printf("  Produce  : %lu   Consume: %lu   Drop: %lu\r\n",
               g_produce_cnt, g_consume_cnt, g_drop_cnt);
        printf("  Flash    : %u pages written (head=%u)\r\n",
               (unsigned)FlashCount, (unsigned)FlashHead);
        printf("============\r\n\r\n");

        OSTimeDlyHMSM(0u, 0u, 2u, 0u);
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

    /* 创建内存分区 */
    LogMemPart = OSMemCreate(LogMemBuf,
                             LOG_POOL_SIZE,
                             LOG_BLOCK_SIZE,
                             &err);

    /* 创建消息队列 */
    LogQueue = OSQCreate(LogQBuf, LOG_POOL_SIZE);

    OSTaskCreate(TaskProducer,
                 (void *)0,
                 &ProducerStk[TASK_STK_SIZE - 1u],
                 PRIO_PRODUCER);

    OSTaskCreate(TaskConsumer,
                 (void *)0,
                 &ConsumerStk[TASK_STK_SIZE - 1u],
                 PRIO_CONSUMER);

    OSTaskCreate(TaskStat,
                 (void *)0,
                 &StatStk[TASK_STK_SIZE - 1u],
                 PRIO_STAT);

    OSStart();

    return 0;
}
