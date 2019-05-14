/*
    FreeRTOS V9.0.0 - Copyright (C) 2016 Real Time Engineers Ltd.
*/

/* Standard includes. */
#include <stdlib.h>
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "StackMacros.h"


#define taskYIELD_IF_USING_PREEMPTION() portYIELD_WITHIN_API()

/* TCB通知状态的取值 */
#define taskNOT_WAITING_NOTIFICATION	( ( uint8_t ) 0 )
#define taskWAITING_NOTIFICATION		( ( uint8_t ) 1 )
#define taskNOTIFICATION_RECEIVED		( ( uint8_t ) 2 )

// 填充字节
#define tskSTACK_FILL_BYTE	( 0xa5U )

#define tskSTATIC_AND_DYNAMIC_ALLOCATION_POSSIBLE ( ( ( configSUPPORT_STATIC_ALLOCATION == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) ) || ( portUSING_MPU_WRAPPERS == 1 ) )
#define tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB 		( ( uint8_t ) 0 )
#define tskSTATICALLY_ALLOCATED_STACK_ONLY 			( ( uint8_t ) 1 )
#define tskSTATICALLY_ALLOCATED_STACK_AND_TCB		( ( uint8_t ) 2 )


/* A port optimised version is provided.  Call the port defined macros. */
#define taskRECORD_READY_PRIORITY( uxPriority )	portRECORD_READY_PRIORITY( uxPriority, uxTopReadyPriority )

/*-----------------------------------------------------------*/

#define taskSELECT_HIGHEST_PRIORITY_TASK()														\
{																								\
	UBaseType_t uxTopPriority;																		\
																									\
	/* Find the highest priority list that contains ready tasks. */								\
	portGET_HIGHEST_PRIORITY( uxTopPriority, uxTopReadyPriority );								\
	configASSERT( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ uxTopPriority ] ) ) > 0 );		\
	listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB, &( pxReadyTasksLists[ uxTopPriority ] ) );		\
} 

/*-----------------------------------------------------------*/

/* A port optimised version is provided, call it only if the TCB being reset
is being referenced from a ready list.  If it is referenced from a delayed
or suspended list then it won't be in a ready list. */
#define taskRESET_READY_PRIORITY( uxPriority )														\
{																									\
	if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ ( uxPriority ) ] ) ) == ( UBaseType_t ) 0 )	\
	{																								\
		portRESET_READY_PRIORITY( ( uxPriority ), ( uxTopReadyPriority ) );							\
	}																								\
}


/*-----------------------------------------------------------*/

/* pxDelayedTaskList and pxOverflowDelayedTaskList are switched when the tick count overflows. */
#define taskSWITCH_DELAYED_LISTS()																	\
{																									\
	List_t *pxTemp;																					\
																									\
	/* The delayed tasks list should be empty when the lists are switched. */						\
	configASSERT( ( listLIST_IS_EMPTY( pxDelayedTaskList ) ) );										\
																									\
	pxTemp = pxDelayedTaskList;																		\
	pxDelayedTaskList = pxOverflowDelayedTaskList;													\
	pxOverflowDelayedTaskList = pxTemp;																\
	xNumOfOverflows++;																				\
	prvResetNextTaskUnblockTime();																	\
}

/*-----------------------------------------------------------*/

/*
 * Place the task represented by pxTCB into the appropriate ready list for
 * the task.  It is inserted at the end of the list.
 */
#define prvAddTaskToReadyList( pxTCB )																\
	traceMOVED_TASK_TO_READY_STATE( pxTCB );														\
	taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority );												\
	vListInsertEnd( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ), &( ( pxTCB )->xStateListItem ) ); \
	tracePOST_MOVED_TASK_TO_READY_STATE( pxTCB )
	
/*-----------------------------------------------------------*/

/*
 * Several functions take an TaskHandle_t parameter that can optionally be NULL,
 * where NULL is used to indicate that the handle of the currently executing
 * task should be used in place of the parameter.  This macro simply checks to
 * see if the parameter is NULL and returns a pointer to the appropriate TCB.
 */
#define prvGetTCBFromHandle( pxHandle ) ( ( ( pxHandle ) == NULL ) ? ( TCB_t * ) pxCurrentTCB : ( TCB_t * ) ( pxHandle ) )


#define taskEVENT_LIST_ITEM_VALUE_IN_USE	0x80000000UL

/* 任务控制块 */
typedef struct tskTaskControlBlock
{
	volatile StackType_t	*pxTopOfStack;	/*任务栈顶指针*/

	ListItem_t			xStateListItem;		/*< The list that the state list item of a task is reference from denotes the state of that task (Ready, Blocked, Suspended ). */
	ListItem_t			xEventListItem;		/*< Used to reference a task from an event list. */
	UBaseType_t			uxPriority;			/*任务优先级 */
	StackType_t			*pxStack;			/*任务栈指针 */
	char				pcTaskName[configMAX_TASK_NAME_LEN];	/*任务名称*/

	// 互斥信号量相关
	UBaseType_t		uxBasePriority;	
	UBaseType_t		uxMutexesHeld;

	// 任务通知功能相关
	volatile uint32_t ulNotifiedValue;
	volatile uint8_t ucNotifyState;
} tskTCB;
typedef tskTCB TCB_t;

// 当前任务管理块，待研究
PRIVILEGED_DATA TCB_t * volatile pxCurrentTCB = NULL;

/* 就绪和阻塞的任务列表 */
PRIVILEGED_DATA static List_t pxReadyTasksLists[configMAX_PRIORITIES];	// 就绪任务
PRIVILEGED_DATA static List_t xDelayedTaskList1;						// 
PRIVILEGED_DATA static List_t xDelayedTaskList2;						/* two lists are used - one for delays that have overflowed the current tick count. */
PRIVILEGED_DATA static List_t * volatile pxDelayedTaskList;				// 指向当前使用的
PRIVILEGED_DATA static List_t * volatile pxOverflowDelayedTaskList;		/*< Points to the delayed task list currently being used to hold tasks that have overflowed the current tick count. */
PRIVILEGED_DATA static List_t xPendingReadyList;						/*< Tasks that have been readied while the scheduler was suspended.  They will be moved to the ready list when the scheduler is resumed. */
//
PRIVILEGED_DATA static List_t xTasksWaitingTermination;					// 已经给删除但内存还未被释放的任务
PRIVILEGED_DATA static volatile UBaseType_t uxDeletedTasksWaitingCleanUp = ( UBaseType_t ) 0U;
//
PRIVILEGED_DATA static List_t xSuspendedTaskList;					// 当前挂起的任务

/* Other file private variables. --------------------------------*/
PRIVILEGED_DATA static volatile UBaseType_t uxCurrentNumberOfTasks 	= ( UBaseType_t ) 0U;  // 当前任务总数量
PRIVILEGED_DATA static volatile TickType_t xTickCount 				= ( TickType_t ) 0U;
PRIVILEGED_DATA static volatile UBaseType_t uxTopReadyPriority 		= tskIDLE_PRIORITY;
PRIVILEGED_DATA static volatile BaseType_t xSchedulerRunning 		= pdFALSE;				// 是否运行调度？
PRIVILEGED_DATA static volatile UBaseType_t uxPendedTicks 			= ( UBaseType_t ) 0U;
PRIVILEGED_DATA static volatile BaseType_t xYieldPending 			= pdFALSE;
PRIVILEGED_DATA static volatile BaseType_t xNumOfOverflows 			= ( BaseType_t ) 0;
PRIVILEGED_DATA static UBaseType_t uxTaskNumber 					= ( UBaseType_t ) 0U;	// 怎么感觉这个一直在加？
PRIVILEGED_DATA static volatile TickType_t xNextTaskUnblockTime		= ( TickType_t ) 0U; /* Initialised to portMAX_DELAY before the scheduler starts. */
PRIVILEGED_DATA static TaskHandle_t xIdleTaskHandle					= NULL;					// 空闲任务的句柄
/* Context switches are held pending while the scheduler is suspended.  Also,
interrupts must not manipulate the xStateListItem of a TCB, or any of the
lists the xStateListItem can be referenced from, if the scheduler is suspended.
If an interrupt needs to unblock a task while the scheduler is suspended then it
moves the task's event list item into the xPendingReadyList, ready for the
kernel to move the task from the pending ready list into the real ready list
when the scheduler is unsuspended.  The pending ready list itself can only be
accessed from a critical section. */
PRIVILEGED_DATA static volatile UBaseType_t uxSchedulerSuspended	= ( UBaseType_t ) pdFALSE;


/*-----------------------------------------------------------*/

static BaseType_t prvTaskIsTaskSuspended( const TaskHandle_t xTask ) PRIVILEGED_FUNCTION;
static void prvInitialiseTaskLists( void ) PRIVILEGED_FUNCTION;

// ?????
static portTASK_FUNCTION_PROTO( prvIdleTask, pvParameters );

/*
 * Utility to free all memory allocated by the scheduler to hold a TCB,
 * including the stack pointed to by the TCB.
 *
 * This does not free memory allocated by the task itself (i.e. memory
 * allocated by calls to pvPortMalloc from within the tasks application code).
 */
static void prvDeleteTCB( TCB_t *pxTCB ) PRIVILEGED_FUNCTION;

//
static void prvCheckTasksWaitingTermination( void ) PRIVILEGED_FUNCTION;

static void prvAddCurrentTaskToDelayedList( TickType_t xTicksToWait, const BaseType_t xCanBlockIndefinitely ) PRIVILEGED_FUNCTION;

static void prvResetNextTaskUnblockTime( void );

static void prvInitialiseNewTask( 	TaskFunction_t pxTaskCode,
									const char * const pcName,
									const uint32_t ulStackDepth,
									void * const pvParameters,
									UBaseType_t uxPriority,
									TaskHandle_t * const pxCreatedTask,
									TCB_t *pxNewTCB,
									const MemoryRegion_t * const xRegions ) PRIVILEGED_FUNCTION; /*lint !e971 Unqualified char types are allowed for strings and single characters only. */

static void prvAddNewTaskToReadyList( TCB_t *pxNewTCB ) PRIVILEGED_FUNCTION;

/*-----------------------------------------------------------*/
// 动态创建任务
BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
							const char * const pcName,
							const uint16_t usStackDepth,
							void * const pvParameters,
							UBaseType_t uxPriority,
							TaskHandle_t * const pxCreatedTask ) 
{
	TCB_t *pxNewTCB;
	BaseType_t xReturn;

	StackType_t *pxStack;

	/* 为任务分配栈空间 */
	pxStack = ( StackType_t * ) pvPortMalloc( ( ( ( size_t ) usStackDepth ) * sizeof( StackType_t ) ) );

	if( pxStack != NULL )
	{
		/* 为TCB(任务控制块)分配空间 */
		pxNewTCB = ( TCB_t * ) pvPortMalloc( sizeof( TCB_t ) ); 

		if( pxNewTCB != NULL )
		{
			/* 存储栈空间到TCB中 */
			pxNewTCB->pxStack = pxStack;
		}
		else
		{
			vPortFree( pxStack );
		}
	}
	else
	{
		pxNewTCB = NULL;
	}
	

	if( pxNewTCB != NULL )
	{
		prvInitialiseNewTask(pxTaskCode, pcName, ( uint32_t ) usStackDepth, pvParameters, uxPriority, pxCreatedTask, pxNewTCB, NULL);
		prvAddNewTaskToReadyList( pxNewTCB );
		xReturn = pdPASS;
	}
	else
	{
		xReturn = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
	}

	return xReturn;
}

/*-----------------------------------------------------------*/
// 初始化新任务
static void prvInitialiseNewTask(TaskFunction_t pxTaskCode,
									const char * const pcName,
									const uint32_t ulStackDepth,
									void * const pvParameters,
									UBaseType_t uxPriority,
									TaskHandle_t * const pxCreatedTask,
									TCB_t *pxNewTCB,
									const MemoryRegion_t * const xRegions)
{
	StackType_t *pxTopOfStack;
	UBaseType_t x;

	pxTopOfStack = pxNewTCB->pxStack + ( ulStackDepth - ( uint32_t ) 1 );
	pxTopOfStack = ( StackType_t * ) ( ( ( portPOINTER_SIZE_TYPE ) pxTopOfStack ) & ( ~( ( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK ) ) ); 

	/* Check the alignment of the calculated top of stack is correct. */
	configASSERT( ( ( ( portPOINTER_SIZE_TYPE ) pxTopOfStack & ( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK ) == 0UL ) );
	

	/*将任务名称保存在TCB中 */
	for( x = ( UBaseType_t ) 0; x < ( UBaseType_t ) configMAX_TASK_NAME_LEN; x++ )
	{
		pxNewTCB->pcTaskName[ x ] = pcName[ x ];
		if( pcName[ x ] == 0x00 )
		{
			break;
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}
	}
	pxNewTCB->pcTaskName[ configMAX_TASK_NAME_LEN - 1 ] = '\0';

	/*检查任务优先级*/
	if( uxPriority >= ( UBaseType_t ) configMAX_PRIORITIES )
	{
		uxPriority = ( UBaseType_t ) configMAX_PRIORITIES - ( UBaseType_t ) 1U;
	}
	else
	{
		mtCOVERAGE_TEST_MARKER();
	}
	pxNewTCB->uxPriority = uxPriority;
	pxNewTCB->uxBasePriority = uxPriority;
	pxNewTCB->uxMutexesHeld = 0;

	vListInitialiseItem( &( pxNewTCB->xStateListItem ) );
	vListInitialiseItem( &( pxNewTCB->xEventListItem ) );

	/* 初始化状态列表项 */
	listSET_LIST_ITEM_OWNER( &( pxNewTCB->xStateListItem ), pxNewTCB );

	/* 初始化事件列表 */
	listSET_LIST_ITEM_VALUE( &( pxNewTCB->xEventListItem ), ( TickType_t ) configMAX_PRIORITIES - ( TickType_t ) uxPriority ); 
	listSET_LIST_ITEM_OWNER( &( pxNewTCB->xEventListItem ), pxNewTCB );

	/*避免编译器警告 */
	( void ) xRegions;

	// 通知相关
	pxNewTCB->ulNotifiedValue = 0;
	pxNewTCB->ucNotifyState = taskNOT_WAITING_NOTIFICATION;

	pxNewTCB->pxTopOfStack = pxPortInitialiseStack(pxTopOfStack, pxTaskCode, pvParameters);

	if( ( void * ) pxCreatedTask != NULL )
	{
		*pxCreatedTask = ( TaskHandle_t ) pxNewTCB;
	}
}

/*-----------------------------------------------------------*/
// 将新任务添加到就绪表中
static void prvAddNewTaskToReadyList( TCB_t *pxNewTCB )
{
	/*确保中断不进入任务列表 */
	taskENTER_CRITICAL();
	{
		uxCurrentNumberOfTasks++;
		if( pxCurrentTCB == NULL )
		{
			// 只有这一个任务，或其它任务都出于挂起状态，将这个任务设为当前任务
			pxCurrentTCB = pxNewTCB;

			if( uxCurrentNumberOfTasks == ( UBaseType_t ) 1 )
			{
				// 这是第一个创建的任务，需要初始化相关全局变量
				prvInitialiseTaskLists();
			}
		}
		else
		{
			if( xSchedulerRunning == pdFALSE )
			{
				if( pxCurrentTCB->uxPriority <= pxNewTCB->uxPriority )
				{
					pxCurrentTCB = pxNewTCB;
				}
			}
		}

		// 为啥只增不减？
		uxTaskNumber++;

		//
		prvAddTaskToReadyList( pxNewTCB );
	}
	taskEXIT_CRITICAL();

	if( xSchedulerRunning != pdFALSE )
	{
		// 如果新建任务优先级比当前运行的优先级还高，那么新建的任务应该立即执行
		if( pxCurrentTCB->uxPriority < pxNewTCB->uxPriority )
		{
			taskYIELD_IF_USING_PREEMPTION();
		}
	}
}

/*-----------------------------------------------------------*/
// 删除任务
void vTaskDelete( TaskHandle_t xTaskToDelete )
{
	TCB_t *pxTCB;

	taskENTER_CRITICAL();
	{
		// 如果参数为空表明要删除的就是当前任务
		pxTCB = prvGetTCBFromHandle( xTaskToDelete );

		// 从任务就绪表中移除任务
		if( uxListRemove( &( pxTCB->xStateListItem ) ) == ( UBaseType_t ) 0 )
		{
			taskRESET_READY_PRIORITY( pxTCB->uxPriority );
		}

		// 任务是否在等待一个事件？
		if( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) != NULL )
		{
			( void ) uxListRemove( &( pxTCB->xEventListItem ) );
		}

		// 这个一直加，可能是调试用的
		uxTaskNumber++;

		if( pxTCB == pxCurrentTCB )
		{
			// 把当前任务放在终止列表中，空闲任务会检查并清除的
			vListInsertEnd( &xTasksWaitingTermination, &( pxTCB->xStateListItem ) );

			// 空闲任务需要清除的任务计数
			++uxDeletedTasksWaitingCleanUp;
		}
		else
		{
			--uxCurrentNumberOfTasks;
			prvDeleteTCB( pxTCB );

			// 重置下一个期望的非阻塞时间
			prvResetNextTaskUnblockTime();
		}
	}
	taskEXIT_CRITICAL();

	// 如果删除的是当前任务，强制重新调度
	if( xSchedulerRunning != pdFALSE )
	{
		if( pxTCB == pxCurrentTCB )
		{
			configASSERT( uxSchedulerSuspended == 0 );
			portYIELD_WITHIN_API();
		}
	}
}


/*-----------------------------------------------------------*/
void vTaskDelay( const TickType_t xTicksToDelay )
{
	BaseType_t xAlreadyYielded = pdFALSE;

	// 如果延时为0，仅仅是强制了一次重新调度
	if( xTicksToDelay > ( TickType_t ) 0U )
	{
		configASSERT( uxSchedulerSuspended == 0 );
		vTaskSuspendAll();
		{
			prvAddCurrentTaskToDelayedList( xTicksToDelay, pdFALSE );
		}
		xAlreadyYielded = xTaskResumeAll();
	}

	// 强制重新调度
	if( xAlreadyYielded == pdFALSE )
	{
		portYIELD_WITHIN_API();
	}
}

/*-----------------------------------------------------------*/
// 获取任务优先级
UBaseType_t uxTaskPriorityGet( TaskHandle_t xTask )
{
	TCB_t *pxTCB;
	UBaseType_t uxReturn;

	taskENTER_CRITICAL();
	{
		// 参数为空表示要获取当前任务的
		pxTCB = prvGetTCBFromHandle( xTask );
		uxReturn = pxTCB->uxPriority;
	}
	taskEXIT_CRITICAL();

	return uxReturn;
}

/*-----------------------------------------------------------*/
// 获取任务优先级(中断级)
UBaseType_t uxTaskPriorityGetFromISR( TaskHandle_t xTask )
{
	TCB_t *pxTCB;
	UBaseType_t uxReturn, uxSavedInterruptState;

	// 断言这个函数只能在中断中调用
	portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

	uxSavedInterruptState = portSET_INTERRUPT_MASK_FROM_ISR();
	{
		pxTCB = prvGetTCBFromHandle( xTask );
		uxReturn = pxTCB->uxPriority;
	}
	portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptState );

	return uxReturn;
}

/*-----------------------------------------------------------*/
// 设置任务优先级
void vTaskPrioritySet( TaskHandle_t xTask, UBaseType_t uxNewPriority )
{
	TCB_t *pxTCB;
	UBaseType_t uxCurrentBasePriority, uxPriorityUsedOnEntry;
	BaseType_t xYieldRequired = pdFALSE;

	// 断言和确保优先级取值范围
	configASSERT( ( uxNewPriority < configMAX_PRIORITIES ) );
	if( uxNewPriority >= ( UBaseType_t ) configMAX_PRIORITIES )
	{
		uxNewPriority = ( UBaseType_t ) configMAX_PRIORITIES - ( UBaseType_t ) 1U;
	}

	taskENTER_CRITICAL();
	{
		// 空代表当前任务
		pxTCB = prvGetTCBFromHandle( xTask );

		uxCurrentBasePriority = pxTCB->uxBasePriority;

		if( uxCurrentBasePriority != uxNewPriority )
		{
			// 
			if( uxNewPriority > uxCurrentBasePriority )
			{
				if( pxTCB != pxCurrentTCB )
				{
					//
					if( uxNewPriority >= pxCurrentTCB->uxPriority )
					{
						xYieldRequired = pdTRUE;
					}
				}
				else
				{
					// 
				}
			}
			else if( pxTCB == pxCurrentTCB )
			{
				// 把当前任务的优先级降低了，意味着可能有更高优先级的任务需要运行
				xYieldRequired = pdTRUE;
			}
			else
			{
				// 把其它任务的优先级降低无所谓
			}

			// ???
			uxPriorityUsedOnEntry = pxTCB->uxPriority;


			// 
			if( pxTCB->uxBasePriority == pxTCB->uxPriority )
			{
				pxTCB->uxPriority = uxNewPriority;
			}
			pxTCB->uxBasePriority = uxNewPriority;
	

			/* Only reset the event list item value if the value is not being used for anything else. */
			if( ( listGET_LIST_ITEM_VALUE( &( pxTCB->xEventListItem ) ) & taskEVENT_LIST_ITEM_VALUE_IN_USE ) == 0UL )
			{
				listSET_LIST_ITEM_VALUE( &( pxTCB->xEventListItem ), ( ( TickType_t ) configMAX_PRIORITIES - ( TickType_t ) uxNewPriority ) );
			}

			/* If the task is in the blocked or suspended list we need do
				nothing more than change it's priority variable. However, if
				the task is in a ready list it needs to be removed and placed
				in the list appropriate to its new priority. */
			if( listIS_CONTAINED_WITHIN( &( pxReadyTasksLists[ uxPriorityUsedOnEntry ] ), &( pxTCB->xStateListItem ) ) != pdFALSE )
			{
					/* The task is currently in its ready list - remove before adding
					it to it's new ready list.  As we are in a critical section we
					can do this even if the scheduler is suspended. */
				if( uxListRemove( &( pxTCB->xStateListItem ) ) == ( UBaseType_t ) 0 )
				{
						/* It is known that the task is in its ready list so
						there is no need to check again and the port level
						reset macro can be called directly. */
					portRESET_READY_PRIORITY( uxPriorityUsedOnEntry, uxTopReadyPriority );
				}
				prvAddTaskToReadyList( pxTCB );
			}

			if( xYieldRequired != pdFALSE )
			{
				taskYIELD_IF_USING_PREEMPTION();
			}

			// 仅仅用来抑制编译器警告
			( void ) uxPriorityUsedOnEntry;
		}
	}
	taskEXIT_CRITICAL();
}

/*-----------------------------------------------------------*/
void vTaskSuspend( TaskHandle_t xTaskToSuspend )
{
	TCB_t *pxTCB;

	taskENTER_CRITICAL();
	{
		// 
		pxTCB = prvGetTCBFromHandle( xTaskToSuspend );

		/* Remove task from the ready/delayed list and place in the suspended list. */
		if( uxListRemove( &( pxTCB->xStateListItem ) ) == ( UBaseType_t ) 0 )
		{
			taskRESET_READY_PRIORITY( pxTCB->uxPriority );
		}

		/* Is the task waiting on an event also? */
		if( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) != NULL )
		{
			( void ) uxListRemove( &( pxTCB->xEventListItem ) );
		}

		vListInsertEnd( &xSuspendedTaskList, &( pxTCB->xStateListItem ) );
	}
	taskEXIT_CRITICAL();

	if( xSchedulerRunning != pdFALSE )
	{
		/* Reset the next expected unblock time in case it referred to the task that is now in the Suspended state. */
		taskENTER_CRITICAL();
		{
			prvResetNextTaskUnblockTime();
		}
		taskEXIT_CRITICAL();
	}

	if( pxTCB == pxCurrentTCB )
	{
		if( xSchedulerRunning != pdFALSE )
		{
			/* The current task has just been suspended. */
			configASSERT( uxSchedulerSuspended == 0 );
			portYIELD_WITHIN_API();
		}
		else
		{
			/* The scheduler is not running, but the task that was pointed
				to by pxCurrentTCB has just been suspended and pxCurrentTCB
				must be adjusted to point to a different task. */
			if( listCURRENT_LIST_LENGTH( &xSuspendedTaskList ) == uxCurrentNumberOfTasks )
			{
				/* No other tasks are ready, so set pxCurrentTCB back to
					NULL so when the next task is created pxCurrentTCB will
					be set to point to it no matter what its relative priority
					is. */
				pxCurrentTCB = NULL;
			}
			else
			{
				vTaskSwitchContext();
			}
		}
	}
}

/*-----------------------------------------------------------*/
static BaseType_t prvTaskIsTaskSuspended( const TaskHandle_t xTask )
{
	BaseType_t xReturn = pdFALSE;
	const TCB_t * const pxTCB = ( TCB_t * ) xTask;

	/* It does not make sense to check if the calling task is suspended. */
	configASSERT( xTask );

	/* Is the task being resumed actually in the suspended list? */
	if( listIS_CONTAINED_WITHIN( &xSuspendedTaskList, &( pxTCB->xStateListItem ) ) != pdFALSE )
	{
		/* Has the task already been resumed from within an ISR? */
		if( listIS_CONTAINED_WITHIN( &xPendingReadyList, &( pxTCB->xEventListItem ) ) == pdFALSE )
		{
			/* Is it in the suspended list because it is in the	Suspended state, or because is is blocked with no timeout? */
			if( listIS_CONTAINED_WITHIN( NULL, &( pxTCB->xEventListItem ) ) != pdFALSE )
			{
				xReturn = pdTRUE;
			}
		}
	}

	return xReturn;
} /*lint !e818 xTask cannot be a pointer to const because it is a typedef. */

/*-----------------------------------------------------------*/
void vTaskResume( TaskHandle_t xTaskToResume )
{
	TCB_t * const pxTCB = ( TCB_t * ) xTaskToResume;

	/* It does not make sense to resume the calling task. */
	configASSERT( xTaskToResume );

	// 恢复的不可能是当前任务
	if( ( pxTCB != NULL ) && ( pxTCB != pxCurrentTCB ) )
	{
		taskENTER_CRITICAL();
		{
			if( prvTaskIsTaskSuspended( pxTCB ) != pdFALSE )
			{
				/* As we are in a critical section we can access the ready lists even if the scheduler is suspended. */
				( void ) uxListRemove(  &( pxTCB->xStateListItem ) );
				prvAddTaskToReadyList( pxTCB );

				/* We may have just resumed a higher priority task. */
				if( pxTCB->uxPriority >= pxCurrentTCB->uxPriority )
				{
					/* This yield may not cause the task just resumed to run,
						but will leave the lists in the correct state for the
						next yield. */
					taskYIELD_IF_USING_PREEMPTION();
				}
			}
		}
		taskEXIT_CRITICAL();
	}
}

/*-----------------------------------------------------------*/
BaseType_t xTaskResumeFromISR( TaskHandle_t xTaskToResume )
{
	BaseType_t xYieldRequired = pdFALSE;
	TCB_t * const pxTCB = ( TCB_t * ) xTaskToResume;
	UBaseType_t uxSavedInterruptStatus;

	configASSERT( xTaskToResume );

	// 断言只能在中断中使用
	portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

	uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR();
	{
		if( prvTaskIsTaskSuspended( pxTCB ) != pdFALSE )
		{
			/* Check the ready lists can be accessed. */
			if( uxSchedulerSuspended == ( UBaseType_t ) pdFALSE )
			{
				/* Ready lists can be accessed so move the task from the
					suspended list to the ready list directly. */
				if( pxTCB->uxPriority >= pxCurrentTCB->uxPriority )
				{
					xYieldRequired = pdTRUE;
				}

				( void ) uxListRemove( &( pxTCB->xStateListItem ) );
				prvAddTaskToReadyList( pxTCB );
			}
			else
			{
				/* The delayed or ready lists cannot be accessed so the task
					is held in the pending ready list until the scheduler is
					unsuspended. */
				vListInsertEnd( &( xPendingReadyList ), &( pxTCB->xEventListItem ) );
			}
		}
	}
	portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );

	return xYieldRequired;
}

/*-----------------------------------------------------------*/
void vTaskStartScheduler( void )
{
	BaseType_t xReturn;

	/* The Idle task is being created using dynamically allocated RAM. */
	xReturn = xTaskCreate(	prvIdleTask,
								"IDLE", configMINIMAL_STACK_SIZE,
								( void * ) NULL,
								( tskIDLE_PRIORITY | portPRIVILEGE_BIT ),
								&xIdleTaskHandle ); 

	if( xReturn == pdPASS )
	{
		/* Interrupts are turned off here, to ensure a tick does not occur
		before or during the call to xPortStartScheduler().  The stacks of
		the created tasks contain a status word with interrupts switched on
		so interrupts will automatically get re-enabled when the first task
		starts to run. */
		portDISABLE_INTERRUPTS();

		xNextTaskUnblockTime = portMAX_DELAY;
		xSchedulerRunning = pdTRUE;
		xTickCount = ( TickType_t ) 0U;

		/* If configGENERATE_RUN_TIME_STATS is defined then the following
		macro must be defined to configure the timer/counter used to generate
		the run time counter time base. */
		portCONFIGURE_TIMER_FOR_RUN_TIME_STATS();

		/* Setting up the timer tick is hardware specific and thus in the
		portable interface. */
		if( xPortStartScheduler() != pdFALSE )
		{
			/* Should not reach here as if the scheduler is running the
			function will not return. */
		}
		else
		{
			/* Should only reach here if a task calls xTaskEndScheduler(). */
		}
	}
	else
	{
		/* This line will only be reached if the kernel could not be started,
		because there was not enough FreeRTOS heap to create the idle task
		or the timer task. */
		configASSERT( xReturn != errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY );
	}

	/* Prevent compiler warnings if INCLUDE_xTaskGetIdleTaskHandle is set to 0,
	meaning xIdleTaskHandle is not used anywhere else. */
	( void ) xIdleTaskHandle;
}

/*-----------------------------------------------------------*/
void vTaskEndScheduler( void )
{
	/* Stop the scheduler interrupts and call the portable scheduler end
	routine so the original ISRs can be restored if necessary.  The port
	layer must ensure interrupts enable	bit is left in the correct state. */
	portDISABLE_INTERRUPTS();
	xSchedulerRunning = pdFALSE;
	vPortEndScheduler();
}
/*----------------------------------------------------------*/

void vTaskSuspendAll( void )
{
	/* A critical section is not required as the variable is of type
	BaseType_t.  Please read Richard Barry's reply in the following link to a
	post in the FreeRTOS support forum before reporting this as a bug! -
	http://goo.gl/wu4acr */
	++uxSchedulerSuspended;
}

/*----------------------------------------------------------*/

BaseType_t xTaskResumeAll( void )
{
	TCB_t *pxTCB = NULL;
	BaseType_t xAlreadyYielded = pdFALSE;

	/* If uxSchedulerSuspended is zero then this function does not match a
	previous call to vTaskSuspendAll(). */
	configASSERT( uxSchedulerSuspended );

	/* It is possible that an ISR caused a task to be removed from an event
	list while the scheduler was suspended.  If this was the case then the
	removed task will have been added to the xPendingReadyList.  Once the
	scheduler has been resumed it is safe to move all the pending ready
	tasks from this list into their appropriate ready list. */
	taskENTER_CRITICAL();
	{
		--uxSchedulerSuspended;

		if( uxSchedulerSuspended == ( UBaseType_t ) pdFALSE )
		{
			if( uxCurrentNumberOfTasks > ( UBaseType_t ) 0U )
			{
				/* Move any readied tasks from the pending list into the
				appropriate ready list. */
				while( listLIST_IS_EMPTY( &xPendingReadyList ) == pdFALSE )
				{
					pxTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( ( &xPendingReadyList ) );
					( void ) uxListRemove( &( pxTCB->xEventListItem ) );
					( void ) uxListRemove( &( pxTCB->xStateListItem ) );
					prvAddTaskToReadyList( pxTCB );

					/* If the moved task has a priority higher than the current
					task then a yield must be performed. */
					if( pxTCB->uxPriority >= pxCurrentTCB->uxPriority )
					{
						xYieldPending = pdTRUE;
					}
					else
					{
						mtCOVERAGE_TEST_MARKER();
					}
				}

				if( pxTCB != NULL )
				{
					/* A task was unblocked while the scheduler was suspended,
					which may have prevented the next unblock time from being
					re-calculated, in which case re-calculate it now.  Mainly
					important for low power tickless implementations, where
					this can prevent an unnecessary exit from low power
					state. */
					prvResetNextTaskUnblockTime();
				}

				/* If any ticks occurred while the scheduler was suspended then
				they should be processed now.  This ensures the tick count does
				not	slip, and that any delayed tasks are resumed at the correct
				time. */
				UBaseType_t uxPendedCounts = uxPendedTicks; /* Non-volatile copy. */
				if( uxPendedCounts > ( UBaseType_t ) 0U )
				{
					do
					{
						if( xTaskIncrementTick() != pdFALSE )
						{
							xYieldPending = pdTRUE;
						}
						else
						{
							mtCOVERAGE_TEST_MARKER();
						}
						--uxPendedCounts;
					} while( uxPendedCounts > ( UBaseType_t ) 0U );

					uxPendedTicks = 0;
				}
				else
				{
					mtCOVERAGE_TEST_MARKER();
				}
				

				/**/
				if( xYieldPending != pdFALSE )
				{
					#if( configUSE_PREEMPTION != 0 )
					{
						xAlreadyYielded = pdTRUE;
					}
					#endif
					taskYIELD_IF_USING_PREEMPTION();
				}
				else
				{
					mtCOVERAGE_TEST_MARKER();
				}
			}
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}
	}
	taskEXIT_CRITICAL();

	return xAlreadyYielded;
}
/*-----------------------------------------------------------*/

TickType_t xTaskGetTickCount( void )
{
	TickType_t xTicks;

	/* Critical section required if running on a 16 bit processor. */
	portTICK_TYPE_ENTER_CRITICAL();
	{
		xTicks = xTickCount;
	}
	portTICK_TYPE_EXIT_CRITICAL();

	return xTicks;
}
/*-----------------------------------------------------------*/

TickType_t xTaskGetTickCountFromISR( void )
{
	TickType_t xReturn;
	UBaseType_t uxSavedInterruptStatus;

	/* RTOS ports that support interrupt nesting have the concept of a maximum
	system call (or maximum API call) interrupt priority.  Interrupts that are
	above the maximum system call priority are kept permanently enabled, even
	when the RTOS kernel is in a critical section, but cannot make any calls to
	FreeRTOS API functions.  If configASSERT() is defined in FreeRTOSConfig.h
	then portASSERT_IF_INTERRUPT_PRIORITY_INVALID() will result in an assertion
	failure if a FreeRTOS API function is called from an interrupt that has been
	assigned a priority above the configured maximum system call priority.
	Only FreeRTOS functions that end in FromISR can be called from interrupts
	that have been assigned a priority at or (logically) below the maximum
	system call	interrupt priority.  FreeRTOS maintains a separate interrupt
	safe API to ensure interrupt entry is as fast and as simple as possible.
	More information (albeit Cortex-M specific) is provided on the following
	link: http://www.freertos.org/RTOS-Cortex-M3-M4.html */
	portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

	uxSavedInterruptStatus = portTICK_TYPE_SET_INTERRUPT_MASK_FROM_ISR();
	{
		xReturn = xTickCount;
	}
	portTICK_TYPE_CLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );

	return xReturn;
}
/*-----------------------------------------------------------*/
// 获取当前任务总数量
UBaseType_t uxTaskGetNumberOfTasks( void )
{
	return uxCurrentNumberOfTasks;
}
/*-----------------------------------------------------------*/

char *pcTaskGetName( TaskHandle_t xTaskToQuery ) /*lint !e971 Unqualified char types are allowed for strings and single characters only. */
{
	TCB_t *pxTCB;

	/* If null is passed in here then the name of the calling task is being
	queried. */
	pxTCB = prvGetTCBFromHandle( xTaskToQuery );
	configASSERT( pxTCB );
	return &( pxTCB->pcTaskName[ 0 ] );
}

/*----------------------------------------------------------*/

BaseType_t xTaskIncrementTick( void )
{
	TCB_t * pxTCB;
	TickType_t xItemValue;
	BaseType_t xSwitchRequired = pdFALSE;

	/* Called by the portable layer each time a tick interrupt occurs.
	Increments the tick then checks to see if the new tick value will cause any
	tasks to be unblocked. */
	traceTASK_INCREMENT_TICK( xTickCount );
	if( uxSchedulerSuspended == ( UBaseType_t ) pdFALSE )
	{
		/* Minor optimisation.  The tick count cannot change in this
		block. */
		const TickType_t xConstTickCount = xTickCount + 1;

		/* Increment the RTOS tick, switching the delayed and overflowed
		delayed lists if it wraps to 0. */
		xTickCount = xConstTickCount;

		if( xConstTickCount == ( TickType_t ) 0U )
		{
			taskSWITCH_DELAYED_LISTS();
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}

		/* See if this tick has made a timeout expire.  Tasks are stored in
		the	queue in the order of their wake time - meaning once one task
		has been found whose block time has not expired there is no need to
		look any further down the list. */
		if( xConstTickCount >= xNextTaskUnblockTime )
		{
			for( ;; )
			{
				if( listLIST_IS_EMPTY( pxDelayedTaskList ) != pdFALSE )
				{
					/* The delayed list is empty.  Set xNextTaskUnblockTime
					to the maximum possible value so it is extremely
					unlikely that the
					if( xTickCount >= xNextTaskUnblockTime ) test will pass
					next time through. */
					xNextTaskUnblockTime = portMAX_DELAY; /*lint !e961 MISRA exception as the casts are only redundant for some ports. */
					break;
				}
				else
				{
					/* The delayed list is not empty, get the value of the
					item at the head of the delayed list.  This is the time
					at which the task at the head of the delayed list must
					be removed from the Blocked state. */
					pxTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( pxDelayedTaskList );
					xItemValue = listGET_LIST_ITEM_VALUE( &( pxTCB->xStateListItem ) );

					if( xConstTickCount < xItemValue )
					{
						/* It is not time to unblock this item yet, but the
						item value is the time at which the task at the head
						of the blocked list must be removed from the Blocked
						state -	so record the item value in
						xNextTaskUnblockTime. */
						xNextTaskUnblockTime = xItemValue;
						break;
					}
					else
					{
						mtCOVERAGE_TEST_MARKER();
					}

					/* It is time to remove the item from the Blocked state. */
					( void ) uxListRemove( &( pxTCB->xStateListItem ) );

					/* Is the task waiting on an event also?  If so remove
					it from the event list. */
					if( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) != NULL )
					{
						( void ) uxListRemove( &( pxTCB->xEventListItem ) );
					}
					else
					{
						mtCOVERAGE_TEST_MARKER();
					}

					/* Place the unblocked task into the appropriate ready
					list. */
					prvAddTaskToReadyList( pxTCB );

					/* A task being unblocked cannot cause an immediate
					context switch if preemption is turned off. */

					/* Preemption is on, but a context switch should
						only be performed if the unblocked task has a
						priority that is equal to or higher than the
						currently executing task. */
					if( pxTCB->uxPriority >= pxCurrentTCB->uxPriority )
					{
						xSwitchRequired = pdTRUE;
					}
					else
					{
						mtCOVERAGE_TEST_MARKER();
					}
				}
			}
		}

		/* Tasks of equal priority to the currently running task will share
		processing time (time slice) if preemption is on, and the application
		writer has not explicitly turned time slicing off. */

		if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ pxCurrentTCB->uxPriority ] ) ) > ( UBaseType_t ) 1 )
		{
			xSwitchRequired = pdTRUE;
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}
	}
	else
	{
		++uxPendedTicks;
	}

	//
	if( xYieldPending != pdFALSE )
	{
		xSwitchRequired = pdTRUE;
	}
	else
	{
		mtCOVERAGE_TEST_MARKER();
	}

	return xSwitchRequired;
}

/*-----------------------------------------------------------*/

void vTaskSwitchContext( void )
{
	if( uxSchedulerSuspended != ( UBaseType_t ) pdFALSE )
	{
		/* The scheduler is currently suspended - do not allow a context
		switch. */
		xYieldPending = pdTRUE;
	}
	else
	{
		xYieldPending = pdFALSE;
		traceTASK_SWITCHED_OUT();

		/* Check for stack overflow, if configured. */
		taskCHECK_FOR_STACK_OVERFLOW();

		/* Select a new task to run using either the generic C or port
		optimised asm code. */
		taskSELECT_HIGHEST_PRIORITY_TASK();
		traceTASK_SWITCHED_IN();
	}
}
/*-----------------------------------------------------------*/

void vTaskPlaceOnEventList( List_t * const pxEventList, const TickType_t xTicksToWait )
{
	configASSERT( pxEventList );

	/* THIS FUNCTION MUST BE CALLED WITH EITHER INTERRUPTS DISABLED OR THE
	SCHEDULER SUSPENDED AND THE QUEUE BEING ACCESSED LOCKED. */

	/* Place the event list item of the TCB in the appropriate event list.
	This is placed in the list in priority order so the highest priority task
	is the first to be woken by the event.  The queue that contains the event
	list is locked, preventing simultaneous access from interrupts. */
	vListInsert( pxEventList, &( pxCurrentTCB->xEventListItem ) );

	prvAddCurrentTaskToDelayedList( xTicksToWait, pdTRUE );
}
/*-----------------------------------------------------------*/

void vTaskPlaceOnUnorderedEventList( List_t * pxEventList, const TickType_t xItemValue, const TickType_t xTicksToWait )
{
	configASSERT( pxEventList );

	/* THIS FUNCTION MUST BE CALLED WITH THE SCHEDULER SUSPENDED.  It is used by
	the event groups implementation. */
	configASSERT( uxSchedulerSuspended != 0 );

	/* Store the item value in the event list item.  It is safe to access the
	event list item here as interrupts won't access the event list item of a
	task that is not in the Blocked state. */
	listSET_LIST_ITEM_VALUE( &( pxCurrentTCB->xEventListItem ), xItemValue | taskEVENT_LIST_ITEM_VALUE_IN_USE );

	/* Place the event list item of the TCB at the end of the appropriate event
	list.  It is safe to access the event list here because it is part of an
	event group implementation - and interrupts don't access event groups
	directly (instead they access them indirectly by pending function calls to
	the task level). */
	vListInsertEnd( pxEventList, &( pxCurrentTCB->xEventListItem ) );

	prvAddCurrentTaskToDelayedList( xTicksToWait, pdTRUE );
}
/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/

BaseType_t xTaskRemoveFromEventList( const List_t * const pxEventList )
{
	TCB_t *pxUnblockedTCB;
	BaseType_t xReturn;

	/* THIS FUNCTION MUST BE CALLED FROM A CRITICAL SECTION.  It can also be
	called from a critical section within an ISR. */

	/* The event list is sorted in priority order, so the first in the list can
	be removed as it is known to be the highest priority.  Remove the TCB from
	the delayed list, and add it to the ready list.

	If an event is for a queue that is locked then this function will never
	get called - the lock count on the queue will get modified instead.  This
	means exclusive access to the event list is guaranteed here.

	This function assumes that a check has already been made to ensure that
	pxEventList is not empty. */
	pxUnblockedTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( pxEventList );
	configASSERT( pxUnblockedTCB );
	( void ) uxListRemove( &( pxUnblockedTCB->xEventListItem ) );

	if( uxSchedulerSuspended == ( UBaseType_t ) pdFALSE )
	{
		( void ) uxListRemove( &( pxUnblockedTCB->xStateListItem ) );
		prvAddTaskToReadyList( pxUnblockedTCB );
	}
	else
	{
		/* The delayed and ready lists cannot be accessed, so hold this task
		pending until the scheduler is resumed. */
		vListInsertEnd( &( xPendingReadyList ), &( pxUnblockedTCB->xEventListItem ) );
	}

	if( pxUnblockedTCB->uxPriority > pxCurrentTCB->uxPriority )
	{
		/* Return true if the task removed from the event list has a higher
		priority than the calling task.  This allows the calling task to know if
		it should force a context switch now. */
		xReturn = pdTRUE;

		/* Mark that a yield is pending in case the user is not using the
		"xHigherPriorityTaskWoken" parameter to an ISR safe FreeRTOS function. */
		xYieldPending = pdTRUE;
	}
	else
	{
		xReturn = pdFALSE;
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

BaseType_t xTaskRemoveFromUnorderedEventList( ListItem_t * pxEventListItem, const TickType_t xItemValue )
{
	TCB_t *pxUnblockedTCB;
	BaseType_t xReturn;

	/* THIS FUNCTION MUST BE CALLED WITH THE SCHEDULER SUSPENDED.  It is used by
	the event flags implementation. */
	configASSERT( uxSchedulerSuspended != pdFALSE );

	/* Store the new item value in the event list. */
	listSET_LIST_ITEM_VALUE( pxEventListItem, xItemValue | taskEVENT_LIST_ITEM_VALUE_IN_USE );

	/* Remove the event list form the event flag.  Interrupts do not access
	event flags. */
	pxUnblockedTCB = ( TCB_t * ) listGET_LIST_ITEM_OWNER( pxEventListItem );
	configASSERT( pxUnblockedTCB );
	( void ) uxListRemove( pxEventListItem );

	/* Remove the task from the delayed list and add it to the ready list.  The
	scheduler is suspended so interrupts will not be accessing the ready
	lists. */
	( void ) uxListRemove( &( pxUnblockedTCB->xStateListItem ) );
	prvAddTaskToReadyList( pxUnblockedTCB );

	if( pxUnblockedTCB->uxPriority > pxCurrentTCB->uxPriority )
	{
		/* Return true if the task removed from the event list has
		a higher priority than the calling task.  This allows
		the calling task to know if it should force a context
		switch now. */
		xReturn = pdTRUE;

		/* Mark that a yield is pending in case the user is not using the
		"xHigherPriorityTaskWoken" parameter to an ISR safe FreeRTOS function. */
		xYieldPending = pdTRUE;
	}
	else
	{
		xReturn = pdFALSE;
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

void vTaskSetTimeOutState( TimeOut_t * const pxTimeOut )
{
	configASSERT( pxTimeOut );
	pxTimeOut->xOverflowCount = xNumOfOverflows;
	pxTimeOut->xTimeOnEntering = xTickCount;
}
/*-----------------------------------------------------------*/

BaseType_t xTaskCheckForTimeOut( TimeOut_t * const pxTimeOut, TickType_t * const pxTicksToWait )
{
	BaseType_t xReturn;

	configASSERT( pxTimeOut );
	configASSERT( pxTicksToWait );

	taskENTER_CRITICAL();
	{
		/* Minor optimisation.  The tick count cannot change in this block. */
		const TickType_t xConstTickCount = xTickCount;

		if( *pxTicksToWait == portMAX_DELAY )
		{
			/* If INCLUDE_vTaskSuspend is set to 1 and the block time
				specified is the maximum block time then the task should block
				indefinitely, and therefore never time out. */
			xReturn = pdFALSE;
		}
		else if( ( xNumOfOverflows != pxTimeOut->xOverflowCount ) && ( xConstTickCount >= pxTimeOut->xTimeOnEntering ) ) /*lint !e525 Indentation preferred as is to make code within pre-processor directives clearer. */
		{
			/* The tick count is greater than the time at which
			vTaskSetTimeout() was called, but has also overflowed since
			vTaskSetTimeOut() was called.  It must have wrapped all the way
			around and gone past again. This passed since vTaskSetTimeout()
			was called. */
			xReturn = pdTRUE;
		}
		else if( ( ( TickType_t ) ( xConstTickCount - pxTimeOut->xTimeOnEntering ) ) < *pxTicksToWait ) /*lint !e961 Explicit casting is only redundant with some compilers, whereas others require it to prevent integer conversion errors. */
		{
			/* Not a genuine timeout. Adjust parameters for time remaining. */
			*pxTicksToWait -= ( xConstTickCount - pxTimeOut->xTimeOnEntering );
			vTaskSetTimeOutState( pxTimeOut );
			xReturn = pdFALSE;
		}
		else
		{
			xReturn = pdTRUE;
		}
	}
	taskEXIT_CRITICAL();

	return xReturn;
}
/*-----------------------------------------------------------*/

void vTaskMissedYield( void )
{
	xYieldPending = pdTRUE;
}
/*-----------------------------------------------------------*/

/*
 * -----------------------------------------------------------
 * The Idle task.
 * ----------------------------------------------------------
 *
 * The portTASK_FUNCTION() macro is used to allow port/compiler specific
 * language extensions.  The equivalent prototype for this function is:
 *
 * void prvIdleTask( void *pvParameters );
 *
 */
static portTASK_FUNCTION( prvIdleTask, pvParameters )
{
	/* Stop warnings. */
	( void ) pvParameters;

	/** THIS IS THE RTOS IDLE TASK - WHICH IS CREATED AUTOMATICALLY WHEN THE
	SCHEDULER IS STARTED. **/

	for( ;; )
	{
		/* See if any tasks have deleted themselves - if so then the idle task
		is responsible for freeing the deleted task's TCB and stack. */
		prvCheckTasksWaitingTermination();


		/* When using preemption tasks of equal priority will be
			timesliced.  If a task that is sharing the idle priority is ready
			to run then the idle task should yield before the end of the
			timeslice.

			A critical region is not required here as we are just reading from
			the list, and an occasional incorrect value will not matter.  If
			the ready list at the idle priority contains more than one task
			then a task other than the idle task is ready to execute. */
		if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ tskIDLE_PRIORITY ] ) ) > ( UBaseType_t ) 1 )
		{
			taskYIELD();
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}
	}
}

/*-----------------------------------------------------------*/

static void prvInitialiseTaskLists( void )
{
	UBaseType_t uxPriority;

	for( uxPriority = ( UBaseType_t ) 0U; uxPriority < ( UBaseType_t ) configMAX_PRIORITIES; uxPriority++ )
	{
		vListInitialise( &( pxReadyTasksLists[ uxPriority ] ) );
	}

	vListInitialise( &xDelayedTaskList1 );
	vListInitialise( &xDelayedTaskList2 );
	vListInitialise( &xPendingReadyList );

	vListInitialise( &xTasksWaitingTermination );

	vListInitialise( &xSuspendedTaskList );

	/* Start with pxDelayedTaskList using list1 and the pxOverflowDelayedTaskList
	using list2. */
	pxDelayedTaskList = &xDelayedTaskList1;
	pxOverflowDelayedTaskList = &xDelayedTaskList2;
}
/*-----------------------------------------------------------*/

static void prvCheckTasksWaitingTermination( void )
{

	/** THIS FUNCTION IS CALLED FROM THE RTOS IDLE TASK **/
	BaseType_t xListIsEmpty;

	/* ucTasksDeleted is used to prevent vTaskSuspendAll() being called
		too often in the idle task. */
	while( uxDeletedTasksWaitingCleanUp > ( UBaseType_t ) 0U )
	{
		vTaskSuspendAll();
		{
			xListIsEmpty = listLIST_IS_EMPTY( &xTasksWaitingTermination );
		}
		( void ) xTaskResumeAll();

		if( xListIsEmpty == pdFALSE )
		{
			TCB_t *pxTCB;

			taskENTER_CRITICAL();
			{
				pxTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( ( &xTasksWaitingTermination ) );
				( void ) uxListRemove( &( pxTCB->xStateListItem ) );
				--uxCurrentNumberOfTasks;
				--uxDeletedTasksWaitingCleanUp;
			}
			taskEXIT_CRITICAL();

			prvDeleteTCB( pxTCB );
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}
	}
	
}

/*-----------------------------------------------------------*/
static void prvDeleteTCB( TCB_t *pxTCB )
{
	/* This call is required specifically for the TriCore port.  It must be
		above the vPortFree() calls.  The call is also used by ports/demos that
		want to allocate and clean RAM statically. */
	portCLEAN_UP_TCB( pxTCB );
	
	/* The task can only have been allocated dynamically - free both the stack and TCB. */
	vPortFree( pxTCB->pxStack );
	vPortFree( pxTCB );	
}

/*-----------------------------------------------------------*/

static void prvResetNextTaskUnblockTime( void )
{
	TCB_t *pxTCB;

	if( listLIST_IS_EMPTY( pxDelayedTaskList ) != pdFALSE )
	{
		/* The new current delayed list is empty.  Set xNextTaskUnblockTime to
		the maximum possible value so it is	extremely unlikely that the
		if( xTickCount >= xNextTaskUnblockTime ) test will pass until
		there is an item in the delayed list. */
		xNextTaskUnblockTime = portMAX_DELAY;
	}
	else
	{
		/* The new current delayed list is not empty, get the value of
		the item at the head of the delayed list.  This is the time at
		which the task at the head of the delayed list should be removed
		from the Blocked state. */
		( pxTCB ) = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( pxDelayedTaskList );
		xNextTaskUnblockTime = listGET_LIST_ITEM_VALUE( &( ( pxTCB )->xStateListItem ) );
	}
}

/*-----------------------------------------------------------*/
TaskHandle_t xTaskGetCurrentTaskHandle( void )
{
	TaskHandle_t xReturn;

	/* A critical section is not required as this is not called from
		an interrupt and the current TCB will always be the same for any
		individual execution thread. */
	xReturn = pxCurrentTCB;

	return xReturn;
}

/*-----------------------------------------------------------*/
BaseType_t xTaskGetSchedulerState( void )
{
	BaseType_t xReturn;

	if( xSchedulerRunning == pdFALSE )
	{
		xReturn = taskSCHEDULER_NOT_STARTED;
	}
	else
	{
		if( uxSchedulerSuspended == ( UBaseType_t ) pdFALSE )
		{
			xReturn = taskSCHEDULER_RUNNING;
		}
		else
		{
			xReturn = taskSCHEDULER_SUSPENDED;
		}
	}

	return xReturn;
}

/*-----------------------------------------------------------*/
void vTaskPriorityInherit( TaskHandle_t const pxMutexHolder )
{
	TCB_t * const pxTCB = ( TCB_t * ) pxMutexHolder;

	/* If the mutex was given back by an interrupt while the queue was
		locked then the mutex holder might now be NULL. */
	if( pxMutexHolder != NULL )
	{
		/* If the holder of the mutex has a priority below the priority of
			the task attempting to obtain the mutex then it will temporarily
			inherit the priority of the task attempting to obtain the mutex. */
		if( pxTCB->uxPriority < pxCurrentTCB->uxPriority )
		{
			/* Adjust the mutex holder state to account for its new
				priority.  Only reset the event list item value if the value is
				not	being used for anything else. */
			if( ( listGET_LIST_ITEM_VALUE( &( pxTCB->xEventListItem ) ) & taskEVENT_LIST_ITEM_VALUE_IN_USE ) == 0UL )
			{
				listSET_LIST_ITEM_VALUE( &( pxTCB->xEventListItem ), ( TickType_t ) configMAX_PRIORITIES - ( TickType_t ) pxCurrentTCB->uxPriority ); /*lint !e961 MISRA exception as the casts are only redundant for some ports. */
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}

			/* If the task being modified is in the ready state it will need
				to be moved into a new list. */
			if( listIS_CONTAINED_WITHIN( &( pxReadyTasksLists[ pxTCB->uxPriority ] ), &( pxTCB->xStateListItem ) ) != pdFALSE )
			{
				if( uxListRemove( &( pxTCB->xStateListItem ) ) == ( UBaseType_t ) 0 )
				{
					taskRESET_READY_PRIORITY( pxTCB->uxPriority );
				}
				else
				{
					mtCOVERAGE_TEST_MARKER();
				}

				/* Inherit the priority before being moved into the new list. */
				pxTCB->uxPriority = pxCurrentTCB->uxPriority;
				prvAddTaskToReadyList( pxTCB );
			}
			else
			{
				/* Just inherit the priority. */
				pxTCB->uxPriority = pxCurrentTCB->uxPriority;
			}

			traceTASK_PRIORITY_INHERIT( pxTCB, pxCurrentTCB->uxPriority );
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}
	}
	else
	{
		mtCOVERAGE_TEST_MARKER();
	}
}

/*-----------------------------------------------------------*/
BaseType_t xTaskPriorityDisinherit( TaskHandle_t const pxMutexHolder )
{
	TCB_t * const pxTCB = ( TCB_t * ) pxMutexHolder;
	BaseType_t xReturn = pdFALSE;

	if( pxMutexHolder != NULL )
	{
		/* A task can only have an inherited priority if it holds the mutex.
			If the mutex is held by a task then it cannot be given from an
			interrupt, and if a mutex is given by the holding task then it must
			be the running state task. */
		configASSERT( pxTCB == pxCurrentTCB );

		configASSERT( pxTCB->uxMutexesHeld );
		( pxTCB->uxMutexesHeld )--;

		/* Has the holder of the mutex inherited the priority of another
			task? */
		if( pxTCB->uxPriority != pxTCB->uxBasePriority )
		{
			/* Only disinherit if no other mutexes are held. */
			if( pxTCB->uxMutexesHeld == ( UBaseType_t ) 0 )
			{
				/* A task can only have an inherited priority if it holds
					the mutex.  If the mutex is held by a task then it cannot be
					given from an interrupt, and if a mutex is given by the
					holding	task then it must be the running state task.  Remove
					the	holding task from the ready	list. */
				if( uxListRemove( &( pxTCB->xStateListItem ) ) == ( UBaseType_t ) 0 )
				{
					taskRESET_READY_PRIORITY( pxTCB->uxPriority );
				}
				else
				{
					mtCOVERAGE_TEST_MARKER();
				}

				/* Disinherit the priority before adding the task into the
					new	ready list. */
				traceTASK_PRIORITY_DISINHERIT( pxTCB, pxTCB->uxBasePriority );
				pxTCB->uxPriority = pxTCB->uxBasePriority;

				/* Reset the event list item value.  It cannot be in use for
					any other purpose if this task is running, and it must be
					running to give back the mutex. */
				listSET_LIST_ITEM_VALUE( &( pxTCB->xEventListItem ), ( TickType_t ) configMAX_PRIORITIES - ( TickType_t ) pxTCB->uxPriority ); /*lint !e961 MISRA exception as the casts are only redundant for some ports. */
				prvAddTaskToReadyList( pxTCB );

				/* Return true to indicate that a context switch is required.
					This is only actually required in the corner case whereby
					multiple mutexes were held and the mutexes were given back
					in an order different to that in which they were taken.
					If a context switch did not occur when the first mutex was
					returned, even if a task was waiting on it, then a context
					switch should occur when the last mutex is returned whether
					a task is waiting on it or not. */
				xReturn = pdTRUE;
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}
	}
	else
	{
		mtCOVERAGE_TEST_MARKER();
	}

	return xReturn;
}

/*-----------------------------------------------------------*/

TickType_t uxTaskResetEventItemValue( void )
{
	TickType_t uxReturn;

	uxReturn = listGET_LIST_ITEM_VALUE( &( pxCurrentTCB->xEventListItem ) );

	/* Reset the event list item to its normal value - so it can be used with
	queues and semaphores. */
	listSET_LIST_ITEM_VALUE( &( pxCurrentTCB->xEventListItem ), ( ( TickType_t ) configMAX_PRIORITIES - ( TickType_t ) pxCurrentTCB->uxPriority ) ); /*lint !e961 MISRA exception as the casts are only redundant for some ports. */

	return uxReturn;
}

/*-----------------------------------------------------------*/
void *pvTaskIncrementMutexHeldCount( void )
{
	/* If xSemaphoreCreateMutex() is called before any tasks have been created
		then pxCurrentTCB will be NULL. */
	if( pxCurrentTCB != NULL )
	{
		( pxCurrentTCB->uxMutexesHeld )++;
	}

	return pxCurrentTCB;
}

/*-----------------------------------------------------------*/
uint32_t ulTaskNotifyTake( BaseType_t xClearCountOnExit, TickType_t xTicksToWait )
{
	uint32_t ulReturn;

	taskENTER_CRITICAL();
	{
		/* Only block if the notification count is not already non-zero. */
		if( pxCurrentTCB->ulNotifiedValue == 0UL )
		{
			/* Mark this task as waiting for a notification. */
			pxCurrentTCB->ucNotifyState = taskWAITING_NOTIFICATION;

			if( xTicksToWait > ( TickType_t ) 0 )
			{
				prvAddCurrentTaskToDelayedList( xTicksToWait, pdTRUE );
				traceTASK_NOTIFY_TAKE_BLOCK();

				/* All ports are written to allow a yield in a critical
					section (some will yield immediately, others wait until the
					critical section exits) - but it is not something that
					application code should ever do. */
				portYIELD_WITHIN_API();
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}
	}
	taskEXIT_CRITICAL();

	taskENTER_CRITICAL();
	{
		traceTASK_NOTIFY_TAKE();
		ulReturn = pxCurrentTCB->ulNotifiedValue;

		if( ulReturn != 0UL )
		{
			if( xClearCountOnExit != pdFALSE )
			{
				pxCurrentTCB->ulNotifiedValue = 0UL;
			}
			else
			{
				pxCurrentTCB->ulNotifiedValue = ulReturn - 1;
			}
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}

		pxCurrentTCB->ucNotifyState = taskNOT_WAITING_NOTIFICATION;
	}
	taskEXIT_CRITICAL();

	return ulReturn;
}

/*-----------------------------------------------------------*/
BaseType_t xTaskNotifyWait( uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit, uint32_t *pulNotificationValue, TickType_t xTicksToWait )
{
	BaseType_t xReturn;

	taskENTER_CRITICAL();
	{
		/* Only block if a notification is not already pending. */
		if( pxCurrentTCB->ucNotifyState != taskNOTIFICATION_RECEIVED )
		{
			/* Clear bits in the task's notification value as bits may get
				set	by the notifying task or interrupt.  This can be used to
				clear the value to zero. */
			pxCurrentTCB->ulNotifiedValue &= ~ulBitsToClearOnEntry;

			/* Mark this task as waiting for a notification. */
			pxCurrentTCB->ucNotifyState = taskWAITING_NOTIFICATION;

			if( xTicksToWait > ( TickType_t ) 0 )
			{
				prvAddCurrentTaskToDelayedList( xTicksToWait, pdTRUE );
				traceTASK_NOTIFY_WAIT_BLOCK();

				/* All ports are written to allow a yield in a critical
					section (some will yield immediately, others wait until the
					critical section exits) - but it is not something that
					application code should ever do. */
				portYIELD_WITHIN_API();
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}
	}
	taskEXIT_CRITICAL();

	taskENTER_CRITICAL();
	{
		traceTASK_NOTIFY_WAIT();

		if( pulNotificationValue != NULL )
		{
			/* Output the current notification value, which may or may not
				have changed. */
			*pulNotificationValue = pxCurrentTCB->ulNotifiedValue;
		}

		/* If ucNotifyValue is set then either the task never entered the
			blocked state (because a notification was already pending) or the
			task unblocked because of a notification.  Otherwise the task
			unblocked because of a timeout. */
		if( pxCurrentTCB->ucNotifyState == taskWAITING_NOTIFICATION )
		{
			/* A notification was not received. */
			xReturn = pdFALSE;
		}
		else
		{
			/* A notification was already pending or a notification was
				received while the task was waiting. */
			pxCurrentTCB->ulNotifiedValue &= ~ulBitsToClearOnExit;
			xReturn = pdTRUE;
		}

		pxCurrentTCB->ucNotifyState = taskNOT_WAITING_NOTIFICATION;
	}
	taskEXIT_CRITICAL();

	return xReturn;
}

/*-----------------------------------------------------------*/
BaseType_t xTaskGenericNotify( TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue )
{
	TCB_t * pxTCB;
	BaseType_t xReturn = pdPASS;
	uint8_t ucOriginalNotifyState;

	configASSERT( xTaskToNotify );
	pxTCB = ( TCB_t * ) xTaskToNotify;

	taskENTER_CRITICAL();
	{
		if( pulPreviousNotificationValue != NULL )
		{
			*pulPreviousNotificationValue = pxTCB->ulNotifiedValue;
		}

		ucOriginalNotifyState = pxTCB->ucNotifyState;

		pxTCB->ucNotifyState = taskNOTIFICATION_RECEIVED;

		switch( eAction )
		{
			case eSetBits	:
				pxTCB->ulNotifiedValue |= ulValue;
				break;

			case eIncrement	:
				( pxTCB->ulNotifiedValue )++;
				break;

			case eSetValueWithOverwrite	:
				pxTCB->ulNotifiedValue = ulValue;
				break;

			case eSetValueWithoutOverwrite :
				if( ucOriginalNotifyState != taskNOTIFICATION_RECEIVED )
				{
					pxTCB->ulNotifiedValue = ulValue;
				}
				else
				{
					/* The value could not be written to the task. */
					xReturn = pdFAIL;
				}
				break;

			case eNoAction:
				/* The task is being notified without its notify value being
					updated. */
				break;
		}

		traceTASK_NOTIFY();

		/* If the task is in the blocked state specifically to wait for a
			notification then unblock it now. */
		if( ucOriginalNotifyState == taskWAITING_NOTIFICATION )
		{
			( void ) uxListRemove( &( pxTCB->xStateListItem ) );
			prvAddTaskToReadyList( pxTCB );

			/* The task should not have been on an event list. */
			configASSERT( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) == NULL );

			if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
			{
				/* The notified task has a priority above the currently
					executing task so a yield is required. */
				taskYIELD_IF_USING_PREEMPTION();
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}
		}
		else
		{
			mtCOVERAGE_TEST_MARKER();
		}
	}
	taskEXIT_CRITICAL();

	return xReturn;
}
/*-----------------------------------------------------------*/


BaseType_t xTaskGenericNotifyFromISR( TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, uint32_t *pulPreviousNotificationValue, BaseType_t *pxHigherPriorityTaskWoken )
{
	TCB_t * pxTCB;
	uint8_t ucOriginalNotifyState;
	BaseType_t xReturn = pdPASS;
	UBaseType_t uxSavedInterruptStatus;

	configASSERT( xTaskToNotify );

	/* RTOS ports that support interrupt nesting have the concept of a
		maximum	system call (or maximum API call) interrupt priority.
		Interrupts that are	above the maximum system call priority are keep
		permanently enabled, even when the RTOS kernel is in a critical section,
		but cannot make any calls to FreeRTOS API functions.  If configASSERT()
		is defined in FreeRTOSConfig.h then
		portASSERT_IF_INTERRUPT_PRIORITY_INVALID() will result in an assertion
		failure if a FreeRTOS API function is called from an interrupt that has
		been assigned a priority above the configured maximum system call
		priority.  Only FreeRTOS functions that end in FromISR can be called
		from interrupts	that have been assigned a priority at or (logically)
		below the maximum system call interrupt priority.  FreeRTOS maintains a
		separate interrupt safe API to ensure interrupt entry is as fast and as
		simple as possible.  More information (albeit Cortex-M specific) is
		provided on the following link:
		http://www.freertos.org/RTOS-Cortex-M3-M4.html */
	portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

	pxTCB = ( TCB_t * ) xTaskToNotify;

	uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR();
	{
		if( pulPreviousNotificationValue != NULL )
		{
			*pulPreviousNotificationValue = pxTCB->ulNotifiedValue;
		}

		ucOriginalNotifyState = pxTCB->ucNotifyState;
		pxTCB->ucNotifyState = taskNOTIFICATION_RECEIVED;

		switch( eAction )
		{
			case eSetBits	:
				pxTCB->ulNotifiedValue |= ulValue;
				break;

			case eIncrement	:
				( pxTCB->ulNotifiedValue )++;
				break;

			case eSetValueWithOverwrite	:
				pxTCB->ulNotifiedValue = ulValue;
				break;

			case eSetValueWithoutOverwrite :
				if( ucOriginalNotifyState != taskNOTIFICATION_RECEIVED )
				{
					pxTCB->ulNotifiedValue = ulValue;
				}
				else
				{
					/* The value could not be written to the task. */
					xReturn = pdFAIL;
				}
				break;

			case eNoAction :
				/* The task is being notified without its notify value being
					updated. */
				break;
		}

		traceTASK_NOTIFY_FROM_ISR();

		/* If the task is in the blocked state specifically to wait for a
			notification then unblock it now. */
		if( ucOriginalNotifyState == taskWAITING_NOTIFICATION )
		{
			/* The task should not have been on an event list. */
			configASSERT( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) == NULL );

			if( uxSchedulerSuspended == ( UBaseType_t ) pdFALSE )
			{
				( void ) uxListRemove( &( pxTCB->xStateListItem ) );
				prvAddTaskToReadyList( pxTCB );
			}
			else
			{
				/* The delayed and ready lists cannot be accessed, so hold
					this task pending until the scheduler is resumed. */
				vListInsertEnd( &( xPendingReadyList ), &( pxTCB->xEventListItem ) );
			}

			if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
			{
				/* The notified task has a priority above the currently
					executing task so a yield is required. */
				if( pxHigherPriorityTaskWoken != NULL )
				{
					*pxHigherPriorityTaskWoken = pdTRUE;
				}
				else
				{
					/* Mark that a yield is pending in case the user is not
						using the "xHigherPriorityTaskWoken" parameter to an ISR
						safe FreeRTOS function. */
					xYieldPending = pdTRUE;
				}
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}
		}
	}
	portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );

	return xReturn;
}

/*-----------------------------------------------------------*/
void vTaskNotifyGiveFromISR( TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken )
{
	TCB_t * pxTCB;
	uint8_t ucOriginalNotifyState;
	UBaseType_t uxSavedInterruptStatus;

	configASSERT( xTaskToNotify );

	/* RTOS ports that support interrupt nesting have the concept of a
		maximum	system call (or maximum API call) interrupt priority.
		Interrupts that are	above the maximum system call priority are keep
		permanently enabled, even when the RTOS kernel is in a critical section,
		but cannot make any calls to FreeRTOS API functions.  If configASSERT()
		is defined in FreeRTOSConfig.h then
		portASSERT_IF_INTERRUPT_PRIORITY_INVALID() will result in an assertion
		failure if a FreeRTOS API function is called from an interrupt that has
		been assigned a priority above the configured maximum system call
		priority.  Only FreeRTOS functions that end in FromISR can be called
		from interrupts	that have been assigned a priority at or (logically)
		below the maximum system call interrupt priority.  FreeRTOS maintains a
		separate interrupt safe API to ensure interrupt entry is as fast and as
		simple as possible.  More information (albeit Cortex-M specific) is
		provided on the following link:
		http://www.freertos.org/RTOS-Cortex-M3-M4.html */
	portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

	pxTCB = ( TCB_t * ) xTaskToNotify;

	uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR();
	{
		ucOriginalNotifyState = pxTCB->ucNotifyState;
		pxTCB->ucNotifyState = taskNOTIFICATION_RECEIVED;

		/* 'Giving' is equivalent to incrementing a count in a counting
			semaphore. */
		( pxTCB->ulNotifiedValue )++;

		traceTASK_NOTIFY_GIVE_FROM_ISR();

		/* If the task is in the blocked state specifically to wait for a
			notification then unblock it now. */
		if( ucOriginalNotifyState == taskWAITING_NOTIFICATION )
		{
			/* The task should not have been on an event list. */
			configASSERT( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) == NULL );

			if( uxSchedulerSuspended == ( UBaseType_t ) pdFALSE )
			{
				( void ) uxListRemove( &( pxTCB->xStateListItem ) );
				prvAddTaskToReadyList( pxTCB );
			}
			else
			{
				/* The delayed and ready lists cannot be accessed, so hold
					this task pending until the scheduler is resumed. */
				vListInsertEnd( &( xPendingReadyList ), &( pxTCB->xEventListItem ) );
			}

			if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
			{
				/* The notified task has a priority above the currently
					executing task so a yield is required. */
				if( pxHigherPriorityTaskWoken != NULL )
				{
					*pxHigherPriorityTaskWoken = pdTRUE;
				}
				else
				{
					/* Mark that a yield is pending in case the user is not
						using the "xHigherPriorityTaskWoken" parameter in an ISR
						safe FreeRTOS function. */
					xYieldPending = pdTRUE;
				}
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}
		}
	}
	portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );
}

/*-----------------------------------------------------------*/
BaseType_t xTaskNotifyStateClear( TaskHandle_t xTask )
{
	TCB_t *pxTCB;
	BaseType_t xReturn;

	/* If null is passed in here then it is the calling task that is having
		its notification state cleared. */
	pxTCB = prvGetTCBFromHandle( xTask );

	taskENTER_CRITICAL();
	{
		if( pxTCB->ucNotifyState == taskNOTIFICATION_RECEIVED )
		{
			pxTCB->ucNotifyState = taskNOT_WAITING_NOTIFICATION;
			xReturn = pdPASS;
		}
		else
		{
			xReturn = pdFAIL;
		}
	}
	taskEXIT_CRITICAL();

	return xReturn;
}

/*-----------------------------------------------------------*/
static void prvAddCurrentTaskToDelayedList( TickType_t xTicksToWait, const BaseType_t xCanBlockIndefinitely )
{
	TickType_t xTimeToWake;
	const TickType_t xConstTickCount = xTickCount;

	/* Remove the task from the ready list before adding it to the blocked list
	as the same list item is used for both lists. */
	if( uxListRemove( &( pxCurrentTCB->xStateListItem ) ) == ( UBaseType_t ) 0 )
	{
		/* The current task must be in a ready list, so there is no need to
		check, and the port reset macro can be called directly. */
		portRESET_READY_PRIORITY( pxCurrentTCB->uxPriority, uxTopReadyPriority );
	}
	else
	{
		mtCOVERAGE_TEST_MARKER();
	}

	//
	if( ( xTicksToWait == portMAX_DELAY ) && ( xCanBlockIndefinitely != pdFALSE ) )
	{
		/* Add the task to the suspended task list instead of a delayed task
			list to ensure it is not woken by a timing event.  It will block
			indefinitely. */
		vListInsertEnd( &xSuspendedTaskList, &( pxCurrentTCB->xStateListItem ) );
	}
	else
	{
		/* Calculate the time at which the task should be woken if the event
			does not occur.  This may overflow but this doesn't matter, the
			kernel will manage it correctly. */
		xTimeToWake = xConstTickCount + xTicksToWait;

		/* The list item will be inserted in wake time order. */
		listSET_LIST_ITEM_VALUE( &( pxCurrentTCB->xStateListItem ), xTimeToWake );

		if( xTimeToWake < xConstTickCount )
		{
			/* Wake time has overflowed.  Place this item in the overflow
				list. */
			vListInsert( pxOverflowDelayedTaskList, &( pxCurrentTCB->xStateListItem ) );
		}
		else
		{
			/* The wake time has not overflowed, so the current block list
				is used. */
			vListInsert( pxDelayedTaskList, &( pxCurrentTCB->xStateListItem ) );

			/* If the task entering the blocked state was placed at the
				head of the list of blocked tasks then xNextTaskUnblockTime
				needs to be updated too. */
			if( xTimeToWake < xNextTaskUnblockTime )
			{
				xNextTaskUnblockTime = xTimeToWake;
			}
			else
			{
				mtCOVERAGE_TEST_MARKER();
			}
		}
	}
	
}

