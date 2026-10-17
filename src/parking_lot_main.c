/*
**********************************************************
* 题目五：基于 uC/OS-II 的智能停车场管理系统仿真
* 编译环境：Keil MDK 4.x  器件：STM32F103C8（软件仿真）
* 说明：计数信号量控制车位，状态机处理计费，
*       消息邮箱传递车牌信息
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
#define TOTAL_SLOTS     5u       /* 仿真时用 5 个车位，方便观察满位阻塞 */
#define PARK_FEE_RATE   2u       /* 每分钟收费（仿真单位：tick/分钟） */

#define PRIO_ENTRY      5u
#define PRIO_EXIT       6u
#define PRIO_BILLING    7u
#define PRIO_DISPLAY    8u

static OS_STK EntryStk  [TASK_STK_SIZE];
static OS_STK ExitStk   [TASK_STK_SIZE];
static OS_STK BillingStk[TASK_STK_SIZE];
static OS_STK DisplayStk[TASK_STK_SIZE];

/*----------------------------------------------------------
 * IPC 对象
 *---------------------------------------------------------*/
static OS_EVENT *SlotSem;       /* 计数信号量：剩余车位 */
static OS_EVENT *EntryMbox;     /* 邮箱：入口 -> 计费 */
static OS_EVENT *ExitMbox;      /* 邮箱：出口 -> 计费 */

/*----------------------------------------------------------
 * 车辆记录
 *---------------------------------------------------------*/
typedef enum
{
    CAR_IDLE    = 0,
    CAR_PARKED  = 1,
    CAR_BILLING = 2
} CarState_t;

typedef struct
{
    char       plate[10];
    INT32U     entry_tick;
    CarState_t state;
} CarRecord_t;

static CarRecord_t  g_cars[TOTAL_SLOTS];
static INT8U        g_slot_used  = 0u;
static INT32U       g_total_fee  = 0u;   /* 累计收费（仿真单位） */
static INT32U       g_car_in_cnt = 0u;   /* 入场总次数 */
static INT32U       g_car_out_cnt= 0u;   /* 出场总次数 */

/*----------------------------------------------------------
 * 找一个空闲车位槽
 *---------------------------------------------------------*/
static INT8U FindFreeSlot(void)
{
    INT8U i;
    for (i = 0u; i < TOTAL_SLOTS; i++)
    {
        if (g_cars[i].state == CAR_IDLE) return i;
    }
    return 0xFFu;   /* 不应出现（信号量已保证有空位） */
}

/*----------------------------------------------------------
 * 找一辆已停车的车（模拟出场：取最早入场的那辆）
 *---------------------------------------------------------*/
static INT8U FindParkedSlot(void)
{
    INT8U i;
    for (i = 0u; i < TOTAL_SLOTS; i++)
    {
        if (g_cars[i].state == CAR_PARKED) return i;
    }
    return 0xFFu;
}

/*----------------------------------------------------------
 * 入口任务：模拟车辆依次进场
 *---------------------------------------------------------*/
static void TaskEntry(void *p_arg)
{
    INT8U  err;
    INT8U  slot;
    (void)p_arg;

    while (1)
    {
        /* 等待空车位（无空位时阻塞，直到出口任务释放信号量） */
        OSSemPend(SlotSem, 0u, &err);

        if (err == OS_ERR_NONE)
        {
            slot = FindFreeSlot();
            if (slot != 0xFFu)
            {
                g_car_in_cnt++;
                snprintf(g_cars[slot].plate,
                         sizeof(g_cars[slot].plate),
                         "A%04lu", g_car_in_cnt);
                g_cars[slot].entry_tick = OSTimeGet();
                g_cars[slot].state      = CAR_PARKED;
                g_slot_used++;

                /* 通知计费任务：有车入场 */
                OSMboxPost(EntryMbox, (void *)&g_cars[slot]);

                printf("[IN ]  %s  used=%u/%u\r\n",
                       g_cars[slot].plate,
                       (unsigned)g_slot_used,
                       (unsigned)TOTAL_SLOTS);
            }
        }

        /* 每隔 1 秒模拟一辆车进场 */
        OSTimeDlyHMSM(0u, 0u, 1u, 0u);
    }
}

/*----------------------------------------------------------
 * 出口任务：每 3 秒模拟一辆车离场
 *---------------------------------------------------------*/
static void TaskExit(void *p_arg)
{
    INT8U slot;
    (void)p_arg;

    while (1)
    {
        OSTimeDlyHMSM(0u, 0u, 3u, 0u);

        slot = FindParkedSlot();
        if (slot != 0xFFu)
        {
            g_cars[slot].state = CAR_BILLING;   /* 进入计费状态 */
            g_car_out_cnt++;

            /* 通知计费任务：有车出场 */
            OSMboxPost(ExitMbox, (void *)&g_cars[slot]);

            /* 释放一个车位信号量 */
            OSSemPost(SlotSem);
            g_slot_used--;

            printf("[OUT]  %s  used=%u/%u\r\n",
                   g_cars[slot].plate,
                   (unsigned)g_slot_used,
                   (unsigned)TOTAL_SLOTS);
        }
    }
}

/*----------------------------------------------------------
 * 计费任务：状态机处理入场/出场消息
 *---------------------------------------------------------*/
static void TaskBilling(void *p_arg)
{
    INT8U       err;
    CarRecord_t *pcar;
    INT32U       duration;
    INT32U       fee;
    (void)p_arg;

    while (1)
    {
        /* 等待出场消息（有出场才需计费） */
        pcar = (CarRecord_t *)OSMboxPend(ExitMbox, 0u, &err);

        if (err == OS_ERR_NONE)
        {
            /* 计算停留时间（tick 数，仿真中 1 tick ≈ 1ms） */
            duration = OSTimeGet() - pcar->entry_tick;

            /* 仿真计费：每 1000 tick 收 1 元 */
            fee = duration / 1000u + 1u;
            g_total_fee += fee;

            printf("[BILL] %s  duration=%lu ticks  fee=%lu yuan"
                   "  total_fee=%lu\r\n",
                   pcar->plate, duration, fee, g_total_fee);

            /* 车位记录归位 */
            pcar->state = CAR_IDLE;
        }
    }
}

/*----------------------------------------------------------
 * 显示任务：每 2 秒刷新停车场状态
 *---------------------------------------------------------*/
static void TaskDisplay(void *p_arg)
{
    INT8U  i;
    INT8U  remain;
    (void)p_arg;

    while (1)
    {
        /* 读取信号量当前值（剩余车位）不阻塞 */
        remain = (INT8U)OSSemAccept(SlotSem);
        /* OSSemAccept 会消耗一个值，需要补回去 */
        if (remain > 0u) OSSemPost(SlotSem);

        printf("\r\n--- Parking Lot Status ---\r\n");
        printf("  Slots     : %u used / %u total\r\n",
               (unsigned)g_slot_used, (unsigned)TOTAL_SLOTS);
        printf("  Total In  : %lu   Total Out: %lu\r\n",
               g_car_in_cnt, g_car_out_cnt);
        printf("  Total Fee : %lu yuan\r\n", g_total_fee);
        printf("  Cars now  :\r\n");
        for (i = 0u; i < TOTAL_SLOTS; i++)
        {
            if (g_cars[i].state == CAR_PARKED)
            {
                printf("    Slot[%u] %s  (in tick=%lu)\r\n",
                       (unsigned)i,
                       g_cars[i].plate,
                       g_cars[i].entry_tick);
            }
        }
        printf("--------------------------\r\n\r\n");

        OSTimeDlyHMSM(0u, 0u, 2u, 0u);
    }
}

/*----------------------------------------------------------
 * main
 *---------------------------------------------------------*/
int main(void)
{
    INT8U i;

    SER_Init();                   /* 初始化串口（printf需要） */
    TIM_Init();                    /* 初始化系统时钟 tick      */
    OSInit();

    /* 初始化车辆记录 */
    for (i = 0u; i < TOTAL_SLOTS; i++)
    {
        g_cars[i].state      = CAR_IDLE;
        g_cars[i].entry_tick = 0u;
        g_cars[i].plate[0]   = '\0';
    }

    /* 创建 IPC 对象 */
    SlotSem    = OSSemCreate(TOTAL_SLOTS);  /* 初始值=总车位数 */
    EntryMbox  = OSMboxCreate((void *)0);
    ExitMbox   = OSMboxCreate((void *)0);

    /* 创建任务 */
    OSTaskCreate(TaskEntry,
                 (void *)0,
                 &EntryStk[TASK_STK_SIZE - 1u],
                 PRIO_ENTRY);

    OSTaskCreate(TaskExit,
                 (void *)0,
                 &ExitStk[TASK_STK_SIZE - 1u],
                 PRIO_EXIT);

    OSTaskCreate(TaskBilling,
                 (void *)0,
                 &BillingStk[TASK_STK_SIZE - 1u],
                 PRIO_BILLING);

    OSTaskCreate(TaskDisplay,
                 (void *)0,
                 &DisplayStk[TASK_STK_SIZE - 1u],
                 PRIO_DISPLAY);

    OSStart();   /* 启动调度器，不返回 */

    return 0;
}
