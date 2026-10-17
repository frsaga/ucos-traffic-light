/****************************************************************************************************
 *                      电子科技大学嵌入式软件工程中心 版权所有										*
 *																									*
 *                  Copyright (C) 2006 UESTC ESEC													*
 ****************************************************************************************************/

/**
 * @file ucos_MessageQueue.c
 * @brief
 *	<li>
 * 		功能:uC/OS消息队列演示
 *	</li>
 *  <li>
 * 		设计思想：这个程序演示基本的消息队列。程序启动设计了6个普通应用任务以及一个控制任务,等待消息的任
 * 务总是按照优先级的高低来决定获得消息的顺 序,实现消息队列的使用。
 *  </li>
 *  <p>
 *  	注：本程序只具备演示功能，不能保证适用于您的真实应用。如需使用，请根据您的实际需要修改本程序。
 *  </p>
 */

/******************************************* 引用部分 ************************************************/
#include "includes.h"

/******************************************* 定义部分 ************************************************/
#define	TASK_STK_SIZE			256     	// Size of each task's stacks (# of WORDs)
#define	N_TASKS					3        	// Number of identical tasks

OS_STK		TaskStk1[N_TASKS][TASK_STK_SIZE];	// Tasks stacks
OS_STK		TaskStk2[N_TASKS][TASK_STK_SIZE];
OS_STK		TaskStartStk[TASK_STK_SIZE];        // Startup task stack
OS_STK		TaskConStk[TASK_STK_SIZE];
INT32U		TaskData1[N_TASKS];                 // Parameters to pass to each task
INT32U		TaskData2[N_TASKS];
OS_EVENT	*q1;                                // 2 queue 
OS_EVENT	*q2;
OS_Q_DATA *data;
void		*Msg1[6];							// 2 arry for msg
void		*Msg2[6];

/******************************************* 声明部分 ************************************************/
static		void	Task1(void *pdata); 		// Function prototypes of tasks
static		void	Task2(void *pdata);
static		void	TaskStart(void *pdata);		// Function prototypes of Startup task
static		void	TaskCon(void *pdata);		//Function prototypes of Control task
static		void	TaskStartCreateTasks(void);	// Function prototypes of creat task

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

	q1 = OSQCreate(&Msg1[0],6);					// set a queue ,  6 is the capacity 
	q2 = OSQCreate(&Msg2[0],6);					// set a queue ,  6 is the capacity 

	OSTaskCreate(TaskStart,  (void *)0,  &TaskStartStk[TASK_STK_SIZE - 1], 0);
	
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
	
	for (i = 0; i <N_TASKS; i++)				// Create N_TASKS identical tasks
	{
		TaskData1[i] = i;						// Each task will pass its own id
		OSTaskCreate(Task1, (void *)&TaskData1[i], &TaskStk1[i][TASK_STK_SIZE - 1], i+1);
	}
	
	for (i = 0; i <N_TASKS; i++)				// Create N_TASKS identical tasks
	{
		TaskData2[i] = i+N_TASKS;						// Each task will pass its own id
		OSTaskCreate(Task2, (void *)&TaskData2[i], &TaskStk2[i][TASK_STK_SIZE - 1], i+N_TASKS+1);
	}
	OSTaskCreate(TaskCon, (void *)0, &TaskConStk[TASK_STK_SIZE - 1],i+5);
}

/**
 * @brief
 * 		Task1()任务入口函数，从队列1中读取消息
 * @param[in]		void *。
 * @return 无
 */
void  Task1 (void *pdata)
{
	INT8U  err;
	INT8U  id;
	void   *mg;
	
	id=*(int *)pdata;
	
	for (;;)
	{
	
		OSTimeDlyHMSM(0, 0, 2, 0);                                   /* Wait 2 second                          */
		mg=OSQPend(q1,0,&err);		                           		 /* apply for message                      */
		switch(err)
		{
			case OS_NO_ERR:
				/* If it is normally, just print the string.*/
				printf("Task_%d GOT %s\n\r",id,(char *)mg);
				OSTimeDlyHMSM(0, 0, 0, 200*(4-id));
				break;
			default :
				/* If the queue is empty or has been deleted, print another string.*/
				printf( "Queue1 : Task %d CANNOT get message !\n\r",id);
				OSTimeDlyHMSM(0, 0, 0, 200*(4-id));
				break;
		}
	}
}

/**
 * @brief
 * 		Task2()任务入口函数，从队列2中读取消息。
 * @param[in]		void *。
 * @return 无
 */
void  Task2 (void *pdata)
{
	INT8U  err;
	INT8U  id;
	void   *mg;
	
	id=*(int *)pdata;
	
	for (;;)
	{
		OSTimeDlyHMSM(0, 0, 2, 0);                                                 /* Wait 2 second                */
		mg=OSQPend(q2,0,&err);                                                     /* apply for message            */
		switch(err)
		{
			case OS_NO_ERR:
				/* If it is normally, just print the string.*/
				printf("Task_%d GOT %s\n\r",id,(char *)mg);
				OSTimeDlyHMSM(0, 0, 0, 200*(7-id));
				break;
			default :
				 /* If the queue is empty or has been deleted, print another string.*/
				printf( "Queue2 : Task %d CANNOT get message!\n\r",id);
				OSTimeDlyHMSM(0, 0, 0, 200*(7-id));
				break;
		}
	}
}

/**
 * @brief
 * 		TaskCon()任务入口函数，为2个队列发送消息，清空队列，删除队列操作
 * @param[in]		void *。
 * @return 无
 */
 
void  TaskCon (void *pdata)
{
	INT8U  i,j;
	INT8U  err;
	INT8U  note=1;                   										/* for flush the queue                  */
	INT16U  del=3;					   										/* for delete the queue                 */
	OS_EVENT *q;
	
	static char *s[]={               										/* queue1's message                      */
						"message0",
						"message1",
						"message2",
						"message3",
						"message4",
						"message5"
						};
	static char *t[]={				   										/* queue2's message                      */
						"messageA",
						"messageB",
						"messageC",
						"messageD",
						"messageE",
						"messageF"
						};
	
	pdata=pdata;
	
	for (;;)
	{
		printf("...........................ADD MESSAGE TO QUEUE_1..............................\n\r");
		for( i = 0 ; i < 6 ; i++ )
		{
			err = OSQPostFront(q1,(void*)s[i]);                       /* post message to q1 LIFO                     */
			switch(err)
			{
				case OS_NO_ERR:
					printf("Queue1 %d add %s\n\r",i,s[i]);
					OSTimeDlyHMSM(0, 0, 0, 150);
					break;
				case OS_Q_FULL:
					printf("Queue1 is full, CANNOT add.\n\r");
					OSTimeDlyHMSM(0, 0, 0, 150);
					break;
				default :
					break;
			}
		}	
			if(del>0)
			{
				printf("...........................ADD MESSAGE TO QUEUE_2..............................\n\r");
			}
			
			for( j = 0 ; j < 6 ; j++ )
			{
				err = OSQPost(q2,(void*)t[j]);			                   /* post message to q2 FIFO                     */
				switch(err)
				{
					case OS_NO_ERR:
						printf("Queue2 %d add %s\n\r",j,t[j]);
						OSTimeDlyHMSM(0, 0, 0, 150);
						break;
					case OS_Q_FULL:
						printf("Queue2 is full, CANNOT add. \n\r");
						OSTimeDlyHMSM(0, 0, 0, 150);
						break;
					default :
						break;
				}
			}
			
			if(del>0)
			{
				if(note==1)
				{
					OSQFlush(q2);
					printf("..............................CLEAR UP QUEUE_2.................................\n\r");
					note=0;
				}
				else
				{
					note=1;
				}
			}
			
			
			OSTimeDlyHMSM(0, 0, 0, 500);                     			     /* Wait 500 minisecond                          */
			if(del==0)
			{
				q=OSQDel(q2,OS_DEL_ALWAYS,&err);                             /* delete the  q2                               */
				if(q==(OS_EVENT *)0)
				{
					printf("DELETE Queue2 OK!\n\r");
				}
			}
			else
			{
				del--;
			}
	}
}



