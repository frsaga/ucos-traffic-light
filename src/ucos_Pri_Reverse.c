/****************************************************************************************************
 *                      电子科技大学嵌入式软件工程中心 版权所有										*
 *																									*
 *                  Copyright (C) 2006 UESTC ESEC													*
 ****************************************************************************************************/


/**
 * @file ucos_Pri_Reverse.c
 * @brief
 *	<li>
 * 		功能:uC/OS优先级反转问题的产生和解决。
 *	</li>
 *  <li>
 * 		设计思想：这个程序演示基本的天花板优先级的解决。程序启动具有三个不同优先级的任务，每个任务请求，获得
 * ，释放互斥信号量，完成一系列动作。
 *  </li>
 *  <p>
 *  	注：本程序只具备演示功能，不能保证适用于您的真实应用。如需使用，请根据您的实际需要修改本程序。
 *  </p>
 */

/******************************************* 引用部分 ************************************************/
#include "includes.h"

/******************************************* 定义部分 ************************************************/
#define	TASK_STK_SIZE			1024       	// Size of each task's stacks (# of WORDs)
#define	N_TASKS					3        	// Number of identical tasks
#define	TASK_START_ID			0			// Application tasks IDs
#define	TASK_START_PRIO			17			// Application tasks priorities

OS_STK		TaskStk[N_TASKS][TASK_STK_SIZE];	// Tasks stacks
OS_STK		TaskStartStk[TASK_STK_SIZE];		// TaskStart  task stack
INT32U		TaskData[N_TASKS];					// Parameters to pass to each task
OS_EVENT	*mutex;								// 1 mutual exclusive semaphore

/******************************************* 声明部分 ************************************************/
static		void	Task0(void *pdata); 		// Function prototypes of tasks
static		void	Task1(void *pdata);
static		void	Task2(void *pdata);
static		void	TaskStart(void *pdata);		// Function prototypes of Startup task
static		void	TaskStartCreateTasks(void);	// Function prototypes of creat task

#define sysPrintf printf
extern void SER_Init(void);                                   /* see Serial.c */
extern void TIM_Init(void);                                   /* see Time.c */

#define PRIORITY_INHERIT_EN           1           /*是否使用ucos-ii的改进型优先级继承策略*/

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

#if (PRIORITY_INHERIT_EN>0)	
	mutex   = OSMutexCreate(8,&err);			// set a mutual semaphore  , 8 is the PIP
#else
	mutex   = OSSemCreate(1);          //create a semaphore
#endif
	
	OSTaskCreate(TaskStart, (void *)0, &TaskStartStk[TASK_STK_SIZE - 1], 4);
	
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
#if OS_CRITICAL_METHOD == 3 					// Allocate storage for CPU status register
	OS_CPU_SR  cpu_sr;
	cpu_sr = cpu_sr;							// Avoid warnings
#endif

	pdata = pdata;								// Prevent compiler warning
	
	TIM_Init ();                  // Initialize system tick timer

	TaskStartCreateTasks();						// Create all the application tasks
	OSTaskSuspend(OS_PRIO_SELF);				// Suspend the TaskStart
}

void doMutexJob(long m)
{
	int i,j;
	for (i = 0; i<m ; i++)
	{
		for (j = 0;j<1000000;j++);
	}
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
	
	for (i = 0; i <N_TASKS; i++)				// Create N_TASKS identical tasks
	{
		TaskData[i] = i;
	}
												// Each task will pass its own id
	OSTaskCreate(Task0, (void *)&TaskData[0], &TaskStk[0][TASK_STK_SIZE - 1], 15);
	OSTaskCreate(Task1, (void *)&TaskData[1], &TaskStk[1][TASK_STK_SIZE - 1], 16);
	OSTaskCreate(Task2, (void *)&TaskData[2], &TaskStk[2][TASK_STK_SIZE - 1], 17);
}

/**
 * @brief
 * 		Task0()函数，最高优先级任务。
 * @param[in]		void *。
 * @return 无
 */
void  Task0 (void *pdata)
{
	INT8U  err;
	INT8U  id;
	
	id=*(int *)pdata;

	OSTimeDly(120);                                       /* Delay 200 clock tick       */

	for (;;)
	{		
		printf("Task_%d (high prority) : trying to GET MUTEX\n\r",id);
		
#if (PRIORITY_INHERIT_EN>0)	
		OSMutexPend(mutex,0,&err);                          /* Acquire mutex */
#else
		OSSemPend(mutex,0,&err);                          /* Acquire mutex */
#endif

		switch(err)
		{
			case OS_NO_ERR:
				printf("Task_%d (high prority) : GOT mutex.\n\r",id);
				break;
			default:
				printf("Task_%d (high prority) : CANNOT get mutex, %d.\n\r",id,err);
		}
		/*OSTimeDly(200);  */                                   /* Delay 200 clock tick       */
		doMutexJob(10);
		printf("Task_%d (high prority) : RELEASE mutex\n\r",id);
#if (PRIORITY_INHERIT_EN>0)	
		OSMutexPost(mutex);                                /* Release mutex         */
#else
		OSSemPost(mutex);                                /* Release mutex         */
#endif
		OSTimeDly(300);  
	}
}

/**
 * @brief
 * 		Task1()函数，中等优先级任务。
 * @param[in]		void *。
 * @return 无
 */
void  Task1 (void *pdata)
{
	INT8U  id;
	id=*(int *)pdata;
	
	OSTimeDly(100);
	for (;;) 
	{
		printf("Task_%d (middle priority) is running\n\r",id);
		/*OSTimeDly(100); */                               /* Delay 100 clock tick       */
		doMutexJob(10);
		printf("Task_%d (middle priority) will delay itself\n\r",id);
		OSTimeDly(200);
	}
}

/**
 * @brief
 * 		Task2()函数，最低优先级任务。
 * @param[in]		void *。
 * @return 无
 */
void  Task2 (void *pdata)
{
	INT8U  err;
	INT8U  id;
	
	id=*(int *)pdata;
	
	for (;;)
	{
		printf("Task_%d(low prority) : trying to GET MUTEX\n\r",id);
#if (PRIORITY_INHERIT_EN>0)	
		OSMutexPend(mutex,0,&err);                          /* Acquire mutex */
#else
		OSSemPend(mutex,0,&err);                          /* Acquire mutex */
#endif
		switch(err)
		{
			case OS_NO_ERR:
				printf("Task_%d (low prority) : GOT mutex.\n\r",id);
				break;
			default :
				printf("Task_%d (low prority) : CANNOT get mutex, %d.\n\r",id,err);
				break;
		}
		doMutexJob(10);
		printf("Task_%d (low prority) : RELEASE mutex\n\r",id);
#if (PRIORITY_INHERIT_EN>0)	
		OSMutexPost(mutex);                                /* Release mutex         */
#else
		OSSemPost(mutex);                                /* Release mutex         */
#endif
		//OSTimeDly(200);  
	}
}


