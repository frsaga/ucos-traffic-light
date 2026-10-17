/****************************************************************************************************
 *                      电子科技大学嵌入式软件工程中心 版权所有										*
 *																									*
 *                  Copyright (C) 2006 UESTC ESEC													*
 ****************************************************************************************************/


/**
 * @file ucos_TaksManager.c
 * @brief
 *	<li>
 * 		功能: uC/OS任务管理演示
 *	</li>
 *  <li>
 * 		设计思想：这个程序演示基本的任务管理。程序启动具有两个不同优先级的任务，完成任务的挂起和被唤醒的系列动作。
 *  </li>
 *  <p>
 *  	注：本程序只具备演示功能，不能保证适用于您的真实应用。如需使用，请根据您的实际需要修改本程序。
 *  </p>
 */

/******************************************* 引用部分 ************************************************/
#include "includes.h"

/******************************************* 定义部分 ************************************************/
#define	TASK_STK_SIZE			512              	// Size of each task's stacks (# of WORDs)
#define	N_TASKS					2                  	// Number of identical tasks
#define	TASK_START_ID			0		             	// Application tasks IDs

OS_STK		TaskStk[N_TASKS][TASK_STK_SIZE];	// Tasks stacks
OS_STK		TaskStartStk[TASK_STK_SIZE];		  // TaskStart  task stack
INT8U		  TaskData[N_TASKS];					      // Parameters to pass to each task

INT32U		count=0;							            // Initialize count
char adc_value[12]="Times:";
/******************************************* 声明部分 ************************************************/

static		void	Task0(void *pdata); 		    // Function prototypes of tasks
static		void	Task1(void *pdata);
static		void	TaskStart(void *pdata);		  // Function prototypes of Startup task
static		void	TaskStartCreateTasks(void);	// Function prototypes of creat task

#define sysPrintf printf
extern void SER_Init(void);                                   /* see Serial.c */
extern void TIM_Init(void);                                   /* see Time.c */

/******************************************* 实现部分 ************************************************/
/**

 * @brief
 * main()函数，应用程序入口函数。
 * @param[in]		无。
 * @return 操作成功返回0；\n
 *         出现问题返回1；
 */
	 
	 
	 
	 
int  main (void)
{
	INT8U err=err;
				
  SER_Init ();                   /* initialize the serial interface           */

	OSInit();									// Initialize uC/OS-II
		
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

/**
 * @brief
 * 		TaskStartCreateTasks()函数，创建多任务。
 * @param[in]		void *。
 * @return 无
 */
void  TaskStartCreateTasks (void)
{
	INT8U  i;
	
	for (i = 0; i < N_TASKS; i++)				// Create tasks
	{
		TaskData[i] =  i;						// Each task will display its own information
	}
	
	OSTaskCreate(Task0, (void *)&TaskData[0], &TaskStk[0][TASK_STK_SIZE - 1], 5);
	OSTaskCreate(Task1, (void *)&TaskData[1], &TaskStk[1][TASK_STK_SIZE - 1], 6);
	
}

/**
 * @brief
 * 		Task0()任务入口函数，反复打印信息和挂起自己。
 * @param[in]		void *。
 * @return 无
 */
void  Task0 (void *pdata)
{
	INT8U i=i;
	INT8U err=err;
 	i=*(int *)pdata;
	for (;;)
	{ 
		
		  sysPrintf("Application tasks switched %d times!\n\r",++count);
		  sysPrintf("TASK_0 IS RUNNING..............................................................\n\r");
		  sysPrintf("task_1 is suspended!\n\r");
		  sysPrintf("*******************************************************************************\n\r");
			
			OSTimeDly(150);
			err=OSTaskSuspend(5);  					// suspend itself
	}
}

/**
 * @brief
 * 		Task1()任务入口函数，反复打印信息和解挂任务5
 * @param[in]		void *。
 * @return 无
 */
void  Task1 (void *pdata)
{
	INT8U i=i;
	INT8U err=err;
 	i=*(int *)pdata;  
	for (;;)
	{
			OSTimeDly(100);
			
		  sysPrintf("Application tasks switched %d times!\n\r",++count);
		  sysPrintf("task_0 is suspended!\n\r");
		  sysPrintf("TASK_1 IS RUNNING..............................................................\n\r");
		  sysPrintf("*******************************************************************************\n\r");
		
		
		
			OSTimeDly(100);
			err=OSTaskResume(5);                                                      /* resume task0           */
		}
}

