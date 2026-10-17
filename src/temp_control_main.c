/*
**********************************************************
* 题目一：基于 uC/OS-II 的智能温控系统
* 编译环境：Keil MDK 4.x  器件：STM32F103C8（软件仿真）
* 说明：无需硬件，全部在 Keil 软件仿真器中运行
*       printf 输出到 Debug (printf) Viewer 窗口
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

/* 任务优先级 */
#define PRIO_SAMPLE     5u
#define PRIO_CTRL       6u
#define PRIO_DISPLAY    7u
#define PRIO_ALARM      8u

/* 任务堆栈 */
static OS_STK SampleStk [TASK_STK_SIZE];
static OS_STK CtrlStk   [TASK_STK_SIZE];
static OS_STK DisplayStk[TASK_STK_SIZE];
static OS_STK AlarmStk  [TASK_STK_SIZE];

/*----------------------------------------------------------
 * 全局变量 & IPC 对象
 *---------------------------------------------------------*/
static OS_EVENT *TempMbox;      /* 邮箱：采样->控制 */
static OS_EVENT *AlarmSem;      /* 信号量：控制->报警 */
static INT16U    g_temp  = 25u; /* 当前温度（仿真用，单位℃） */
static INT16U    g_alarm = 0u;  /* 报警次数 */

/* 报警阈值 */
#define TEMP_THRESHOLD  60u

/*----------------------------------------------------------
 * 仿真温度函数：每次调用温度+1，到 80 后回到 20
 *---------------------------------------------------------*/
static INT16U Sim_GetTemp(void)
{
    static INT16U sim = 20u;
    sim++;
    if (sim > 80u) sim = 20u;
    return sim;
}

/*----------------------------------------------------------
 * 任务函数
 *---------------------------------------------------------*/

/* 采样任务：每 500ms 采集一次温度，通过邮箱发给控制任务 */
static void TaskSample(void *p_arg)
{
    (void)p_arg;
    while (1)
    {
        g_temp = Sim_GetTemp();                 /* 读温度（仿真） */
        OSMboxPost(TempMbox, (void *)&g_temp);  /* 发送邮箱 */
        OSTimeDlyHMSM(0u, 0u, 0u, 500u);
    }
}

/* 控制任务：收到温度后判断是否超阈值 */
static void TaskCtrl(void *p_arg)
{
    INT8U   err;
    INT16U *ptemp;
    (void)p_arg;
    while (1)
    {
        ptemp = (INT16U *)OSMboxPend(TempMbox, 0u, &err);
        if (err == OS_ERR_NONE)
        {
            if (*ptemp > TEMP_THRESHOLD)
            {
                OSSemPost(AlarmSem);            /* 超温，触发报警 */
            }
        }
    }
}

/* 显示任务：每 200ms 刷新一次串口输出 */
static void TaskDisplay(void *p_arg)
{
    (void)p_arg;
    while (1)
    {
        printf("[DISPLAY] Temp = %d C  Alarm count = %d\r\n",
               (int)g_temp, (int)g_alarm);
        OSTimeDlyHMSM(0u, 0u, 0u, 200u);
    }
}

/* 报警任务：等待信号量，模拟蜂鸣器响 1 秒 */
static void TaskAlarm(void *p_arg)
{
    INT8U err;
    (void)p_arg;
    while (1)
    {
        OSSemPend(AlarmSem, 0u, &err);
        if (err == OS_ERR_NONE)
        {
            g_alarm++;
            printf("[ALARM]  Over threshold! Temp = %d C  (Buzzer ON)\r\n",
                   (int)g_temp);
            OSTimeDlyHMSM(0u, 0u, 1u, 0u);     /* 模拟蜂鸣 1 秒 */
            printf("[ALARM]  Buzzer OFF\r\n");
        }
    }
}

/*----------------------------------------------------------
 * main
 *---------------------------------------------------------*/
int main(void)
{
    SER_Init();                   /* 初始化串口（printf需要） */
    TIM_Init();                    /* 初始化系统时钟 tick      */
    OSInit();                                   /* uC/OS-II 初始化 */

    TempMbox = OSMboxCreate((void *)0);         /* 创建邮箱 */
    AlarmSem = OSSemCreate(0u);                 /* 创建信号量，初值0 */

    OSTaskCreate(TaskSample,
                 (void *)0,
                 &SampleStk[TASK_STK_SIZE - 1u],
                 PRIO_SAMPLE);

    OSTaskCreate(TaskCtrl,
                 (void *)0,
                 &CtrlStk[TASK_STK_SIZE - 1u],
                 PRIO_CTRL);

    OSTaskCreate(TaskDisplay,
                 (void *)0,
                 &DisplayStk[TASK_STK_SIZE - 1u],
                 PRIO_DISPLAY);

    OSTaskCreate(TaskAlarm,
                 (void *)0,
                 &AlarmStk[TASK_STK_SIZE - 1u],
                 PRIO_ALARM);

    OSStart();                                  /* 启动调度器，不返回 */

    return 0;
}
