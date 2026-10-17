/*
**********************************************************
* 题目二：基于 uC/OS-II 的多任务串口通信管理系统
* 编译环境：Keil MDK 4.x  器件：STM32F103C8（软件仿真）
* 演示：互斥量保护共享资源、消息队列传递数据、
*       优先级反转对比实验
**********************************************************
*/

#include <stdio.h>
#include "includes.h"

extern void SER_Init(void);   /* Serial.c  */
extern void TIM_Init(void);   /* Time.c    */
#include <string.h>



/*----------------------------------------------------------
 * 任务配置
 *---------------------------------------------------------*/
#define TASK_STK_SIZE   256u
#define MSG_Q_SIZE      16u

#define PRIO_SEND1      5u      /* 高优先级发送 */
#define PRIO_SEND2      9u      /* 低优先级发送 */
#define PRIO_RECV       7u      /* 接收处理 */

static OS_STK Send1Stk[TASK_STK_SIZE];
static OS_STK Send2Stk[TASK_STK_SIZE];
static OS_STK RecvStk [TASK_STK_SIZE];

/*----------------------------------------------------------
 * IPC 对象
 *---------------------------------------------------------*/
static OS_EVENT *UartMutex;             /* 互斥量，天花板优先级=4 */
static OS_EVENT *MsgQueue;              /* 消息队列 */
static void     *MsgBuf[MSG_Q_SIZE];    /* 队列缓冲区 */

/* 仿真"串口发送"缓冲区 */
static char TxBuf1[64];
static char TxBuf2[64];
static INT32U g_send1_cnt = 0u;
static INT32U g_send2_cnt = 0u;

/*----------------------------------------------------------
 * 模拟串口发送（实际仿真中就是 printf）
 *---------------------------------------------------------*/
static void USART_SimSend(const char *str)
{
    printf("%s", str);
}

/*----------------------------------------------------------
 * 任务函数
 *---------------------------------------------------------*/

/* 高优先级发送任务：每 300ms 发一条消息 */
static void TaskSend1(void *p_arg)
{
    INT8U err;
    (void)p_arg;
    while (1)
    {
        OSMutexPend(UartMutex, 0u, &err);       /* 申请串口互斥量 */
        if (err == OS_ERR_NONE)
        {
            g_send1_cnt++;
            snprintf(TxBuf1, sizeof(TxBuf1),
                     "[SEND1] msg#%lu  (high priority)\r\n",
                     g_send1_cnt);
            USART_SimSend(TxBuf1);

            /* 同时把消息投入队列供接收任务处理 */
            OSQPost(MsgQueue, (void *)TxBuf1);

            OSMutexPost(UartMutex);             /* 释放互斥量 */
        }
        OSTimeDlyHMSM(0u, 0u, 0u, 300u);
    }
}

/* 低优先级发送任务：每 700ms 发一条消息 */
static void TaskSend2(void *p_arg)
{
    INT8U err;
    (void)p_arg;
    while (1)
    {
        OSMutexPend(UartMutex, 0u, &err);       /* 申请串口互斥量 */
        if (err == OS_ERR_NONE)
        {
            g_send2_cnt++;
            snprintf(TxBuf2, sizeof(TxBuf2),
                     "[SEND2] msg#%lu  (low priority)\r\n",
                     g_send2_cnt);
            USART_SimSend(TxBuf2);

            OSQPost(MsgQueue, (void *)TxBuf2);

            OSMutexPost(UartMutex);
        }
        OSTimeDlyHMSM(0u, 0u, 0u, 700u);
    }
}

/* 接收处理任务：从队列取出消息并解析打印 */
static void TaskRecv(void *p_arg)
{
    INT8U  err;
    char  *pmsg;
    INT32U recv_cnt = 0u;
    (void)p_arg;
    while (1)
    {
        pmsg = (char *)OSQPend(MsgQueue, 0u, &err);
        if (err == OS_ERR_NONE)
        {
            recv_cnt++;
            printf("[RECV ] #%lu -> %s", recv_cnt, pmsg);
        }
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

    /* 创建互斥量，优先级天花板 = 4（比所有任务优先级高） */
    UartMutex = OSMutexCreate(4u, &err);

    /* 创建消息队列 */
    MsgQueue = OSQCreate(MsgBuf, MSG_Q_SIZE);

    OSTaskCreate(TaskSend1,
                 (void *)0,
                 &Send1Stk[TASK_STK_SIZE - 1u],
                 PRIO_SEND1);

    OSTaskCreate(TaskSend2,
                 (void *)0,
                 &Send2Stk[TASK_STK_SIZE - 1u],
                 PRIO_SEND2);

    OSTaskCreate(TaskRecv,
                 (void *)0,
                 &RecvStk[TASK_STK_SIZE - 1u],
                 PRIO_RECV);

    OSStart();

    return 0;
}
