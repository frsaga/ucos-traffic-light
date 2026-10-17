/****************************************************************************************************
 *                      电子科技大学嵌入式软件工程中心 版权所有										*
 *																									*
 *                  Copyright (C) 2006 UESTC ESEC													*
 ****************************************************************************************************/

/**
 * @file ucos_MemManager.c
 * @brief
 *	<li>
 * 		功能: 演示内核的内存分配策略
 *	</li>
 *  <li>
 * 		设计思想：这个程序演示主要体现内核的内存分配思想的策略，包含内存的分配和释放
 *  </li>
 *  <p>
 *  	注：本程序只具备演示功能，不能保证适用于您的真实应用。如需使用，请根据您的实际需要修改本程序。
 *  </p>
 */

/******************************************* 引用部分 ************************************************/
#include "includes.h"

/******************************************* 定义部分 ************************************************/
#define	TASK_STK_SIZE			512       	// Size of each task's stacks (# of WORDs)
#define	N_TASKS					3        	// Number of identical tasks

#define	TASK_START_ID			0			// Application tasks IDs
#define	TASK_START_PRIO			10			// Application tasks priorities
#define	TASK_1_PRIO				12
#define	TASK_2_PRIO				13

OS_STK		TaskStk[N_TASKS][TASK_STK_SIZE];	// Tasks stacks
OS_STK		TaskStartStk[TASK_STK_SIZE];		// TaskStart  task stack
INT32U		TaskData[N_TASKS];					// Parameters to pass to each task

void		*CommMsg[3];							//为OSMemGet函数设置返回值存储。
OS_MEM		*CommMem;							//为OSMemCreate函数设置返回值存储。
INT8U		CommBuf[2][128];					//初始化内存分配的空间。
OS_EVENT	*mutex;								// 1 mutual exclusive semaphore

/******************************************* 声明部分 ************************************************/
static		void	MemoryCreate(void);
static 		void	MemInfo(void* /*pdata*/);		//内存空间信息。
static		void	DispShow(void);
static 		void	ReleaseMem(int /*i*/);
static		void	Task0(void* /*pdata*/); 		// Function prototypes of tasks
static		void	TaskStart(void* /*pdata*/);		// Function prototypes of Startup task
static		void	TaskStartCreateTasks(void);	// Function prototypes of creat task

extern void SER_Init(void);                                   /* see Serial.c */
extern void TIM_Init(void);                                   /* see Time.c */

/******************************************* 实现部分 ************************************************/

/**
 * @brief
 * 		MemoryCreate()函数，创建一个内存分区。
 * @param[in]		无。
 * @return 无
 */
static void MemoryCreate()
{
	//为OSMemCreate函数设置包含错误码的变量的指针。
	INT8U err;
	//OSMemCreate（）函数建立并初始化一块内存区。2块，每块128B。
	CommMem = OSMemCreate(&CommBuf[0][0], 2, 128, &err);		
}

/**
 * @brief
 * 		MemInfo()函数，获得内存分区信息.
 * @param[in]		缓冲区指针。
 * @return 无
 */
void MemInfo(void *pdata)	//内存空间信息。
{
	INT8U        err=err;		//为函数设置包含错误码的变量指针。
	OS_MEM_DATA mem_data;
	
	pdata = pdata;
	
	err = OSMemQuery(CommMem, &mem_data);	//OSMemQuery（）函数得到内存区的信息。
}

/**
 * @brief
 * 		DispShow()函数，打印内存分区信息。
 * @param[in]		无。
 * @return 操作成功返回0；\n
 *         出现问题返回1；
 */
static void DispShow()
{
	
	OS_MEM_DATA mem_data;
	
	OSMemQuery(CommMem, &mem_data);
	
	printf( "Begining Address of memory:\t%X\n\r",(int)(mem_data.OSAddr));
	printf( "Block Size in the memory area:\t%d\n\r",(int)(mem_data.OSBlkSize));
	printf( "Free Blocks in the memory area:\t%d\n\r", (int)(mem_data.OSNFree));
	printf( "Used Blocks in the memory area:\t%d\n\r", (int)mem_data.OSNUsed);
	printf("\n\n\n");
	OSTimeDlyHMSM(0,0,1,0);
}

/**
 * @brief
 * 		ReleaseMem()函数，内存释放。
 * @param[in]		无。
 * @return 操作成功返回0；\n
 *         出现问题返回1；
 */
void ReleaseMem(int i)
{
	INT8U err;

	err=OSMemPut(CommMem,CommMsg[i]);
	if (err == OS_NO_ERR)
	{
		printf("%d:Released memory.\n\r",i);                    /* 释放内存块     */
	}
	else
	{
		printf("%d:NOT Exist memory_2.\n\r",i);
	}
}

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

	MemoryCreate();
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
	OSTaskCreate(Task0, (void *)&TaskData[0], &TaskStk[0][TASK_STK_SIZE - 1], 5);
}

/**
 * @brief
 * 		Task0()函数，执行内存分配和释放的任务。
 * @param[in]		void *。
 * @return 无
 */
void  Task0 (void *pdata)
{
	INT8U  err;
	
	INT16S i=0;

	MemInfo(pdata);
	DispShow();
	OSTimeDly(20);                 		   /* Wait 20tick                  */
	
	
	for (;;) 
	{
		switch(i){
		case 0:
		case 1:
		case 2:
			//申请内存区中的一块。
			CommMsg[i]=OSMemGet(CommMem,&err);		//申请内存区中的一块。
			if (err == OS_NO_ERR)
			{
				printf("%d:GOT memory .\n\r",i+1);		//内存块已经分配
			}
			else
			{
				printf("%d: memory alloc FAILED!!!!! .\n\r",i+1);
			}
			MemInfo(pdata);							//函数得到内存区的信息。
			DispShow();								//将内存区信息显示到屏幕上。
			OSTimeDly(20);                			// Wait 20tick
			i++;
			break;
		case 3:
			for(i=2;i>0;i--)
			{
				ReleaseMem(i);			     		//释放一个内存块。
				MemInfo(pdata);						//函数得到内存区的信息
				DispShow();							//将内存区信息显示到屏幕上。
				OSTimeDly(10);						//Wait 10tick
			}
			break;
		}			
	}
}


