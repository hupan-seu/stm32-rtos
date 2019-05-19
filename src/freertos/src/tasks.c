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

/*-----------------------------------------------------------*/
// 下面两个，一个是记录就绪的优先级标志位，一个是清除就绪的优先级标志位(uxTopReadyPriority)
#define taskRECORD_READY_PRIORITY( uxPriority )	portRECORD_READY_PRIORITY( uxPriority, uxTopReadyPriority )
#define taskRESET_READY_PRIORITY( uxPriority )														\
{																									\
	if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ ( uxPriority ) ] ) ) == ( UBaseType_t ) 0 )	\
	{																								\
		portRESET_READY_PRIORITY( ( uxPriority ), ( uxTopReadyPriority ) );							\
	}																								\
}

/*-----------------------------------------------------------*/
// 将下一个就绪的最高优先级任务，放入到pxCurrentTCB中
// 用到了上面的就绪优先级标志位
#define taskSELECT_HIGHEST_PRIORITY_TASK()														\
{																								\
	UBaseType_t uxTopPriority;																	\
																								\
	/* Find the highest priority list that contains ready tasks. */								\
	portGET_HIGHEST_PRIORITY( uxTopPriority, uxTopReadyPriority );								\
	configASSERT( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ uxTopPriority ] ) ) > 0 );		\
	listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB, &( pxReadyTasksLists[ uxTopPriority ] ) );		\
} 

/*-----------------------------------------------------------*/
// 把任务添加到就绪表中
#define prvAddTaskToReadyList( pxTCB )																\
	taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority );												\
	vListInsertEnd( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ), &( ( pxTCB )->xStateListItem ) );


/*-----------------------------------------------------------*/
// 延时相关的操作，待研究
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
// 判断传入参数是否为空，空代表当前任务
#define prvGetTCBFromHandle( pxHandle ) ( ( ( pxHandle ) == NULL ) ? ( TCB_t * ) pxCurrentTCB : ( TCB_t * ) ( pxHandle ) )

#define taskEVENT_LIST_ITEM_VALUE_IN_USE	0x80000000UL


/* 任务控制块 */
typedef struct tskTaskControlBlock
{
	volatile StackType_t	*pxTopOfStack;	/*任务栈顶指针*/

	ListItem_t			xStateListItem;		// 被放在就绪表，挂起表，延时表等 /*< The list that the state list item of a task is reference from denotes the state of that task (Ready, Blocked, Suspended ). */
	ListItem_t			xEventListItem;		// 指向事件列表中的一个任务
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
PRIVILEGED_DATA static List_t pxReadyTasksLists[configMAX_PRIORITIES];	// 就绪任务，按优先级划分成数组
PRIVILEGED_DATA static List_t xDelayedTaskList1;						// 使用两个列表，当tick溢出时，放另一个里面
PRIVILEGED_DATA static List_t xDelayedTaskList2;						//
PRIVILEGED_DATA static List_t * volatile pxDelayedTaskList;				// 指向当前使用的
PRIVILEGED_DATA static List_t * volatile pxOverflowDelayedTaskList;		// 指向tick溢出的 
PRIVILEGED_DATA static List_t xPendingReadyList;						/*< Tasks that have been readied while the scheduler was suspended.  They will be moved to the ready list when the scheduler is resumed. */
//
PRIVILEGED_DATA static List_t xTasksWaitingTermination;											// 已经给删除但内存还未被释放的任务
PRIVILEGED_DATA static volatile UBaseType_t uxDeletedTasksWaitingCleanUp = ( UBaseType_t ) 0U;	// 等着被释放的数量
//
PRIVILEGED_DATA static List_t xSuspendedTaskList;						// 当前挂起的任务

/* Other file private variables. --------------------------------*/
PRIVILEGED_DATA static volatile UBaseType_t uxCurrentNumberOfTasks 	= ( UBaseType_t ) 0U;  	// 当前任务总数量
PRIVILEGED_DATA static volatile TickType_t xTickCount 				= ( TickType_t ) 0U;	// tick计数
PRIVILEGED_DATA static volatile UBaseType_t uxTopReadyPriority 		= tskIDLE_PRIORITY;		// 已就绪任务的优先级标志位，按位存储中
PRIVILEGED_DATA static volatile BaseType_t xSchedulerRunning 		= pdFALSE;				// 调度是否处于运行状态
PRIVILEGED_DATA static volatile UBaseType_t uxPendedTicks 			= ( UBaseType_t ) 0U;
PRIVILEGED_DATA static volatile BaseType_t xYieldPending 			= pdFALSE;
PRIVILEGED_DATA static volatile BaseType_t xNumOfOverflows 			= ( BaseType_t ) 0;
PRIVILEGED_DATA static UBaseType_t uxTaskNumber 					= ( UBaseType_t ) 0U;	// 怎么感觉这个一直在加？
PRIVILEGED_DATA static volatile TickType_t xNextTaskUnblockTime		= ( TickType_t ) 0U; 	// 下一个任务非阻塞时间，调度开始前初始化成 portMAX_DELAY
PRIVILEGED_DATA static TaskHandle_t xIdleTaskHandle					= NULL;					// 空闲任务的句柄
/* Context switches are held pending while the scheduler is suspended.  Also,
interrupts must not manipulate the xStateListItem of a TCB, or any of the
lists the xStateListItem can be referenced from, if the scheduler is suspended.
If an interrupt needs to unblock a task while the scheduler is suspended then it
moves the task's event list item into the xPendingReadyList, ready for the
kernel to move the task from the pending ready list into the real ready list
when the scheduler is unsuspended.  The pending ready list itself can only be
accessed from a critical section. */
PRIVILEGED_DATA static volatile UBaseType_t uxSchedulerSuspended	= ( UBaseType_t ) pdFALSE;	// 待研究


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

	// 将任务名称保存在TCB中
	for( x = ( UBaseType_t ) 0; x < ( UBaseType_t ) configMAX_TASK_NAME_LEN; x++ )
	{
		pxNewTCB->pcTaskName[ x ] = pcName[ x ];
		if( pcName[ x ] == 0x00 )
		{
			break;
		}
	}
	pxNewTCB->pcTaskName[ configMAX_TASK_NAME_LEN - 1 ] = '\0';

	// 检查任务优先级
	if( uxPriority >= ( UBaseType_t ) configMAX_PRIORITIES )
	{
		uxPriority = ( UBaseType_t ) configMAX_PRIORITIES - ( UBaseType_t ) 1U;
	}
	pxNewTCB->uxPriority = uxPriority;
	pxNewTCB->uxBasePriority = uxPriority;
	pxNewTCB->uxMutexesHeld = 0;

	vListInitialiseItem( &( pxNewTCB->xStateListItem ) );
	vListInitialiseItem( &( pxNewTCB->xEventListItem ) );

	// 初始化状态列表项
	listSET_LIST_ITEM_OWNER( &( pxNewTCB->xStateListItem ), pxNewTCB );

	// 初始化事件列表
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
// 使当前任务延时一段时间
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
					// 修改的不是当前，且修改后的优先级比当前高，需要重新调度
					if( uxNewPriority >= pxCurrentTCB->uxPriority )
					{
						xYieldRequired = pdTRUE;
					}
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
	

			// 待研究
			if( ( listGET_LIST_ITEM_VALUE( &( pxTCB->xEventListItem ) ) & taskEVENT_LIST_ITEM_VALUE_IN_USE ) == 0UL )
			{
				listSET_LIST_ITEM_VALUE( &( pxTCB->xEventListItem ), ( ( TickType_t ) configMAX_PRIORITIES - ( TickType_t ) uxNewPriority ) );
			}

			// 如果这个任务在就绪表中，需要挪个位置
			if( listIS_CONTAINED_WITHIN( &( pxReadyTasksLists[ uxPriorityUsedOnEntry ] ), &( pxTCB->xStateListItem ) ) != pdFALSE )
			{
				if( uxListRemove( &( pxTCB->xStateListItem ) ) == ( UBaseType_t ) 0 )
				{
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
// 将某个任务挂起
void vTaskSuspend( TaskHandle_t xTaskToSuspend )
{
	TCB_t *pxTCB;

	taskENTER_CRITICAL();
	{
		// 
		pxTCB = prvGetTCBFromHandle( xTaskToSuspend );

		// 将任务从 就绪/延时 列表中移除，放在挂起列表中
		if( uxListRemove( &( pxTCB->xStateListItem ) ) == ( UBaseType_t ) 0 )
		{
			taskRESET_READY_PRIORITY( pxTCB->uxPriority );
		}

		// 如果在事件列表中，也移除掉
		if( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) != NULL )
		{
			( void ) uxListRemove( &( pxTCB->xEventListItem ) );
		}

		vListInsertEnd( &xSuspendedTaskList, &( pxTCB->xStateListItem ) );
	}
	taskEXIT_CRITICAL();

	if( xSchedulerRunning != pdFALSE )
	{
		// 防止当前任务时延时任务的第一个
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
			if( listCURRENT_LIST_LENGTH( &xSuspendedTaskList ) == uxCurrentNumberOfTasks )
			{
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
// 
static BaseType_t prvTaskIsTaskSuspended( const TaskHandle_t xTask )
{
	BaseType_t xReturn = pdFALSE;
	const TCB_t * const pxTCB = ( TCB_t * ) xTask;

	configASSERT( xTask );

	if( listIS_CONTAINED_WITHIN( &xSuspendedTaskList, &( pxTCB->xStateListItem ) ) != pdFALSE )
	{
		if( listIS_CONTAINED_WITHIN( &xPendingReadyList, &( pxTCB->xEventListItem ) ) == pdFALSE )
		{
			if( listIS_CONTAINED_WITHIN( NULL, &( pxTCB->xEventListItem ) ) != pdFALSE )
			{
				xReturn = pdTRUE;
			}
		}
	}

	return xReturn;
}

/*-----------------------------------------------------------*/
// 
void vTaskResume( TaskHandle_t xTaskToResume )
{
	TCB_t * const pxTCB = ( TCB_t * ) xTaskToResume;

	configASSERT( xTaskToResume );

	// 恢复的不可能是当前任务
	if( ( pxTCB != NULL ) && ( pxTCB != pxCurrentTCB ) )
	{
		taskENTER_CRITICAL();
		{
			if( prvTaskIsTaskSuspended( pxTCB ) != pdFALSE )
			{
				( void ) uxListRemove(  &( pxTCB->xStateListItem ) );
				prvAddTaskToReadyList( pxTCB );

				if( pxTCB->uxPriority >= pxCurrentTCB->uxPriority )
				{
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
			if( uxSchedulerSuspended == ( UBaseType_t ) pdFALSE )
			{
				if( pxTCB->uxPriority >= pxCurrentTCB->uxPriority )
				{
					xYieldRequired = pdTRUE;
				}

				( void ) uxListRemove( &( pxTCB->xStateListItem ) );
				prvAddTaskToReadyList( pxTCB );
			}
			else
			{
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

	// 创建空闲任务
	xReturn = xTaskCreate(	prvIdleTask,
								"IDLE", configMINIMAL_STACK_SIZE,
								( void * ) NULL,
								( tskIDLE_PRIORITY | portPRIVILEGE_BIT ),
								&xIdleTaskHandle ); 

	if( xReturn == pdPASS )
	{
		// 关闭中断
		// 第一个任务运行时中断会自动打开
		portDISABLE_INTERRUPTS();

		xNextTaskUnblockTime = portMAX_DELAY;
		xSchedulerRunning = pdTRUE;
		xTickCount = ( TickType_t ) 0U;

		// 启动调度
		if( xPortStartScheduler() != pdFALSE )
		{
			// 不应该运行到这里
		}
		else
		{
			// 只应该运行到这里
		}
	}
	else
	{
		// 运行到这里只可能是内存不足
		configASSERT( xReturn != errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY );
	}

	// 抑制警告
	( void ) xIdleTaskHandle;
}

/*-----------------------------------------------------------*/
void vTaskEndScheduler( void )
{
	portDISABLE_INTERRUPTS();
	xSchedulerRunning = pdFALSE;
	vPortEndScheduler();
}
/*----------------------------------------------------------*/

void vTaskSuspendAll( void )
{
	++uxSchedulerSuspended;
}

/*----------------------------------------------------------*/

BaseType_t xTaskResumeAll( void )
{
	TCB_t *pxTCB = NULL;
	BaseType_t xAlreadyYielded = pdFALSE;

	configASSERT( uxSchedulerSuspended );

	taskENTER_CRITICAL();
	{
		--uxSchedulerSuspended;

		if( uxSchedulerSuspended == ( UBaseType_t ) pdFALSE )
		{
			if( uxCurrentNumberOfTasks > ( UBaseType_t ) 0U )
			{
				// 把就绪的任务从挂起列表移到就绪列表
				while( listLIST_IS_EMPTY( &xPendingReadyList ) == pdFALSE )
				{
					pxTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( ( &xPendingReadyList ) );
					( void ) uxListRemove( &( pxTCB->xEventListItem ) );
					( void ) uxListRemove( &( pxTCB->xStateListItem ) );
					prvAddTaskToReadyList( pxTCB );

					// 如果有更高优先级的任务，需要重置调度
					if( pxTCB->uxPriority >= pxCurrentTCB->uxPriority )
					{
						xYieldPending = pdTRUE;
					}
				}

				if( pxTCB != NULL )
				{
					prvResetNextTaskUnblockTime();
				}

				UBaseType_t uxPendedCounts = uxPendedTicks; 
				if( uxPendedCounts > ( UBaseType_t ) 0U )
				{
					do
					{
						if( xTaskIncrementTick() != pdFALSE )
						{
							xYieldPending = pdTRUE;
						}
						--uxPendedCounts;
					} while( uxPendedCounts > ( UBaseType_t ) 0U );

					uxPendedTicks = 0;
				}
				
				//
				if( xYieldPending != pdFALSE )
				{
					xAlreadyYielded = pdTRUE;
					taskYIELD_IF_USING_PREEMPTION();
				}
			}
		}
	}
	taskEXIT_CRITICAL();

	return xAlreadyYielded;
}

/*-----------------------------------------------------------*/
TickType_t xTaskGetTickCount( void )
{
	TickType_t xTicks;

	// 16位处理器是非原子操作
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
// 获取任务名称
char *pcTaskGetName( TaskHandle_t xTaskToQuery )
{
	TCB_t *pxTCB;

	pxTCB = prvGetTCBFromHandle( xTaskToQuery );
	configASSERT( pxTCB );
	return &( pxTCB->pcTaskName[ 0 ] );
}

/*----------------------------------------------------------*/
// 待研究
BaseType_t xTaskIncrementTick( void )
{
	TCB_t * pxTCB;
	TickType_t xItemValue;
	BaseType_t xSwitchRequired = pdFALSE;

	if( uxSchedulerSuspended == ( UBaseType_t ) pdFALSE )
	{
		// Minor
		const TickType_t xConstTickCount = xTickCount + 1;

		//
		xTickCount = xConstTickCount;

		if( xConstTickCount == ( TickType_t ) 0U )
		{
			taskSWITCH_DELAYED_LISTS();
		}

		// 
		if( xConstTickCount >= xNextTaskUnblockTime )
		{
			for( ;; )
			{
				if( listLIST_IS_EMPTY( pxDelayedTaskList ) != pdFALSE )
				{
					// 延时列表为空
					xNextTaskUnblockTime = portMAX_DELAY;
					break;
				}
				else
				{
					// 
					pxTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( pxDelayedTaskList );
					xItemValue = listGET_LIST_ITEM_VALUE( &( pxTCB->xStateListItem ) );

					if( xConstTickCount < xItemValue )
					{
						//
						xNextTaskUnblockTime = xItemValue;
						break;
					}

					// 
					( void ) uxListRemove( &( pxTCB->xStateListItem ) );

					//
					if( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) != NULL )
					{
						( void ) uxListRemove( &( pxTCB->xEventListItem ) );
					}

					// 放到就绪表中
					prvAddTaskToReadyList( pxTCB );

					if( pxTCB->uxPriority >= pxCurrentTCB->uxPriority )
					{
						xSwitchRequired = pdTRUE;
					}
				}
			}
		}

		if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ pxCurrentTCB->uxPriority ] ) ) > ( UBaseType_t ) 1 )
		{
			xSwitchRequired = pdTRUE;
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
	
	return xSwitchRequired;
}

/*-----------------------------------------------------------*/
void vTaskSwitchContext( void )
{
	if( uxSchedulerSuspended != ( UBaseType_t ) pdFALSE )
	{
		// 调度被挂起，不允许切换上下文
		xYieldPending = pdTRUE;
	}
	else
	{
		xYieldPending = pdFALSE;

		// 检查栈溢出
		taskCHECK_FOR_STACK_OVERFLOW();

		// 选下一个要运行的任务
		taskSELECT_HIGHEST_PRIORITY_TASK();
	}
}

/*-----------------------------------------------------------*/
void vTaskPlaceOnEventList( List_t * const pxEventList, const TickType_t xTicksToWait )
{
	configASSERT( pxEventList );

	vListInsert( pxEventList, &( pxCurrentTCB->xEventListItem ) );

	prvAddCurrentTaskToDelayedList( xTicksToWait, pdTRUE );
}

/*-----------------------------------------------------------*/
void vTaskPlaceOnUnorderedEventList( List_t * pxEventList, const TickType_t xItemValue, const TickType_t xTicksToWait )
{
	configASSERT( pxEventList );

	configASSERT( uxSchedulerSuspended != 0 );

	listSET_LIST_ITEM_VALUE( &( pxCurrentTCB->xEventListItem ), xItemValue | taskEVENT_LIST_ITEM_VALUE_IN_USE );

	vListInsertEnd( pxEventList, &( pxCurrentTCB->xEventListItem ) );

	prvAddCurrentTaskToDelayedList( xTicksToWait, pdTRUE );
}

/*-----------------------------------------------------------*/
BaseType_t xTaskRemoveFromEventList( const List_t * const pxEventList )
{
	TCB_t *pxUnblockedTCB;
	BaseType_t xReturn;

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
// 空闲任务
// portTASK_FUNCTION() 是一个宏，等效的表示方法为 void prvIdleTask( void *pvParameters );
static portTASK_FUNCTION( prvIdleTask, pvParameters )
{
	/* Stop warnings. */
	( void ) pvParameters;

	// 调度开始时空闲任务自动创建

	for( ;; )
	{
		// 检查有没有自删除的任务，帮其删除内存空间
		prvCheckTasksWaitingTermination();

		// 让和空闲任务处于同等优先级的任务运行
		if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ tskIDLE_PRIORITY ] ) ) > ( UBaseType_t ) 1 )
		{
			taskYIELD();
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

	/* ucTasksDeleted is used to prevent vTaskSuspendAll() being called too often in the idle task. */
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
	}
	
}

/*-----------------------------------------------------------*/
static void prvDeleteTCB( TCB_t *pxTCB )
{	
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
		// 如果延时列表为空，重置为最大值
		xNextTaskUnblockTime = portMAX_DELAY;
	}
	else
	{
		// 重置成延时列表第一项的时间
		( pxTCB ) = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( pxDelayedTaskList );
		xNextTaskUnblockTime = listGET_LIST_ITEM_VALUE( &( ( pxTCB )->xStateListItem ) );
	}
}

/*-----------------------------------------------------------*/
TaskHandle_t xTaskGetCurrentTaskHandle( void )
{
	TaskHandle_t xReturn;

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

	/* If the mutex was given back by an interrupt while the queue was locked then the mutex holder might now be NULL. */
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

			/* If the task being modified is in the ready state it will need
				to be moved into a new list. */
			if( listIS_CONTAINED_WITHIN( &( pxReadyTasksLists[ pxTCB->uxPriority ] ), &( pxTCB->xStateListItem ) ) != pdFALSE )
			{
				if( uxListRemove( &( pxTCB->xStateListItem ) ) == ( UBaseType_t ) 0 )
				{
					taskRESET_READY_PRIORITY( pxTCB->uxPriority );
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

		/* Has the holder of the mutex inherited the priority of another task? */
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

				/* Disinherit the priority before adding the task into the new	ready list. */
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
		}
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
// 把当前任务放到延时列表中
static void prvAddCurrentTaskToDelayedList( TickType_t xTicksToWait, const BaseType_t xCanBlockIndefinitely )
{
	TickType_t xTimeToWake;
	const TickType_t xConstTickCount = xTickCount;

	// 先把当前任务从就绪表中移除
	if( uxListRemove( &( pxCurrentTCB->xStateListItem ) ) == ( UBaseType_t ) 0 )
	{
		// 当前优先级的就绪列表为空，重置就绪标志位
		portRESET_READY_PRIORITY( pxCurrentTCB->uxPriority, uxTopReadyPriority );
	}

	//
	if( ( xTicksToWait == portMAX_DELAY ) && ( xCanBlockIndefinitely != pdFALSE ) )
	{
		// 延时最大，直接放到挂起列表中
		vListInsertEnd( &xSuspendedTaskList, &( pxCurrentTCB->xStateListItem ) );
	}
	else
	{
		// 计算醒来的时间tick
		xTimeToWake = xConstTickCount + xTicksToWait;

		// 醒来的时间tick设置为列表项的值
		listSET_LIST_ITEM_VALUE( &( pxCurrentTCB->xStateListItem ), xTimeToWake );

		if( xTimeToWake < xConstTickCount )
		{
			// 如果醒来的tick小于当前tick，说明已经溢出
			vListInsert( pxOverflowDelayedTaskList, &( pxCurrentTCB->xStateListItem ) );
		}
		else
		{
			// 未溢出，放入当前延时表中
			vListInsert( pxDelayedTaskList, &( pxCurrentTCB->xStateListItem ) );

			// 更新下次unblock时间
			if( xTimeToWake < xNextTaskUnblockTime )
			{
				xNextTaskUnblockTime = xTimeToWake;
			}
		}
	}
}

