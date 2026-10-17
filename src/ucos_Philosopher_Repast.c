/****************************************************************************************************
 *                      电子科技大学嵌入式软件工程中心 版权所有										*
 *																									*
 *                  Copyright (C) 2006 UESTC ESEC													*
 ****************************************************************************************************/


/**
 * @file ucos_Philosopher_Repast.c
 * @brief
 *	<li>
 * 		功能:uC/OS哲学家就餐演示
 *	</li>
 *  <li>
 * 		设计思想：这个程序演示基本的哲学家就餐问题的解决。程序启动具有五个不同优先级的任务，每个任务对应一个
 * 哲学家，完成一系列动作.
 *  </li>
 *  <p>
 *  	注：本程序只具备演示功能，不能保证适用于您的真实应用。如需使用，请根据您的实际需要修改本程序。
 *  </p>
 */

/******************************************* 引用部分 ************************************************/
#include "includes.h"

/******************************************* 定义部分 ************************************************/
#define	TASK_STK_SIZE			512       	// Size of each task's stacks (# of WORDs)
#define	N_TASKS					5        	// Number of identical tasks
#define	TASK_START_ID			0			// Application tasks IDs
#define	TASK_START_PRIO			0			// Application tasks priorities

typedef struct {
    char    TaskName[30];
    INT16U  TaskCtr;
    INT16U  TaskExecTime;
    INT32U  TaskTotExecTime;
} TASK_USER_DATA;


OS_STK		TaskStk[N_TASKS][TASK_STK_SIZE];	// Tasks stacks
OS_STK		TaskStartStk[TASK_STK_SIZE];		// TaskStart  task stack
TASK_USER_DATA	TaskUserData[7];
OS_EVENT	*fork[N_TASKS];  					// 5 tasks semaphore
INT32U		TaskData[N_TASKS];					// Parameters to pass to each task

/******************************************* 声明部分 ************************************************/
static		void	TaskPhilosopher(void *pdata); 		// Function prototypes of tasks
static		void	TaskStart(void *pdata);		// Function prototypes of Startup task
static		void	TaskStartCreateTasks(void);	// Function prototypes of creat task
static		void	ProcThinking2Hungry(INT8U);
static		void	ProcEat(INT8U);

#define sysPrintf printf
extern void SER_Init(void);                                   /* see Serial.c */
extern void TIM_Init(void);                                   /* see Time.c */

/******************************************* 实现部分 ************************************************/
/**
 * @brief
 * 		main()函数，应用程序入口函数。
 * @param[in]		无。
 * @return 操作成功返回0；\n
 *         出现问题返回1；
 */
int  main (void)
{
	INT8U err=err;
	
  SER_Init ();                   /* initialize the serial interface           */
	OSInit();									// Initialize uC/OS-II
	
	OSTaskCreateExt(TaskStart,
					(void *)0,
					&TaskStartStk[TASK_STK_SIZE - 1],
					TASK_START_PRIO,
					TASK_START_ID,
					&TaskStartStk[0],
					TASK_STK_SIZE,
					&TaskUserData[TASK_START_ID],
					0);	
	OSStart();									// Start multitasking
	return 0;
}

/**
 * @brief
 * 		TaskStart()函数，启动任务。
 * @param[in]		void *。
 * @return 无\n
 */
void	TaskStart(void *pdata)
{
	int i;
#if OS_CRITICAL_METHOD == 3 					// Allocate storage for CPU status register
	OS_CPU_SR  cpu_sr;
	cpu_sr = cpu_sr;							// Avoid warnings
#endif

	
	pdata = pdata;								// Prevent compiler warning
	
	TIM_Init ();                  // Initialize system tick timer

	for (i=0 ;i<N_TASKS;i++ )					// Create N_TASKS identical tasks
	{
		fork[i]   = OSSemCreate(1);				// each create a signal
	}

	TaskStartCreateTasks();						// Create all the application tasks
	OSTaskSuspend(OS_PRIO_SELF);				// Suspend the TaskStart
}

/**
 * @brief
 * 		TaskStartCreateTasks()函数，创建多任务。
 * @param[in]		void *。
 * @return 无
 */
void  TaskStartCreateTasks (void)
{
	INT8U  i;
	
	i = rand();
	
	for (i = 0; i <N_TASKS; i++) 				// Create N_TASKS identical tasks
	{
		TaskData[i] = i;						// Each task will pass its own id
		OSTaskCreate(TaskPhilosopher, (void *)&TaskData[i], &TaskStk[i][TASK_STK_SIZE - 1], i+1);
	}
}

/**
 * @brief
 * 		ProcThinking2Hungry()函数，模拟哲学家思考和产生饥饿的过程
 * @param[in]		void *。
 * @return 无
 */
static void ProcThinking2Hungry(INT8U data)
{
	sysPrintf("Philosopher %d is thinking...\n\r", data+1);
	
	OSTimeDly(100);
	
	sysPrintf("Philosopher %d is hungry!\n\r", data+1);
	
	OSTimeDly(100);
} 
/**
 * @brief
 * 		ProcEat()函数，模拟哲学家eating的过程
 * @param[in]		void *。
 * @return 无
 */
static void ProcEat(INT8U data)
{
	sysPrintf("......Philosopher %d is EATING......\n\r", data+1);	
	OSTimeDly(100);
}

/**
 * @brief
 * 		TaskPhilosopher()任务入口函数，模拟每个哲学家。
 * @param[in]		void *。
 * @return 无
 */
void  TaskPhilosopher (void *pdata)
{
	INT8U  err;
	INT8U  i;
	INT8U  j;
	
	i=*(int *)pdata;
	j=(i+1) % 5;
	
	for (;;)
	{
		ProcThinking2Hungry(i);
		OSSemPend(fork[i], 0, &err);
		OSSemPend(fork[j], 0, &err);       /* Acquire semaphores to eat */

		ProcEat(i);
		OSSemPost(fork[j]);
		OSSemPost(fork[i]);                /* Release semaphore         */

		OSTimeDly(200);                    /* Delay 10 clock tick       */
	}
}

