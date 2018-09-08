/*
    FreeRTOS V9.0.0 - Copyright (C) 2016 Real Time Engineers Ltd.
*/

#ifndef INC_FREERTOS_H
	#error FreeRTOS.h must be included before list.h
#endif

#ifndef LIST_H
#define LIST_H


#ifdef __cplusplus
extern "C" {
#endif


/* 列表项结构体定义 */
struct xLIST_ITEM
{
	TickType_t xItemValue;			/* 列表项值 */
	struct xLIST_ITEM * pxNext;		/* 下一个列表项 */
	struct xLIST_ITEM * pxPrevious;	/* 前一个列表项 */
	void * pvOwner;					/* 所有者，通常是任务控制块 TCB_t */
	void * pvContainer;				/* 指回那个列表？ */
};
typedef struct xLIST_ITEM ListItem_t;

/* 迷你列表项定义 */
struct xMINI_LIST_ITEM
{
	TickType_t xItemValue;
	struct xLIST_ITEM * pxNext;
	struct xLIST_ITEM * pxPrevious;
};
typedef struct xMINI_LIST_ITEM MiniListItem_t;

/* 列表结构体定义 */
typedef struct xLIST
{
	UBaseType_t uxNumberOfItems;	/* 列表项数量 */
	ListItem_t * pxIndex;			/* 当前列表项索引 */
	MiniListItem_t xListEnd;		/* 列表中最后一个列表项 */
} List_t;


/* */
#define listSET_LIST_ITEM_OWNER( pxListItem, pxOwner )		( ( pxListItem )->pvOwner = ( void * ) ( pxOwner ) )
#define listGET_LIST_ITEM_OWNER( pxListItem )	( ( pxListItem )->pvOwner )


/* */
#define listSET_LIST_ITEM_VALUE( pxListItem, xValue )	( ( pxListItem )->xItemValue = ( xValue ) )
#define listGET_LIST_ITEM_VALUE( pxListItem )	( ( pxListItem )->xItemValue )


/* 获取列表的head列表项的值 */
#define listGET_ITEM_VALUE_OF_HEAD_ENTRY( pxList )	( ( ( pxList )->xListEnd ).pxNext->xItemValue )
/* 获取列表的head列表项 */
#define listGET_HEAD_ENTRY( pxList )	( ( ( pxList )->xListEnd ).pxNext )

/* 获取下一个列表项 */
#define listGET_NEXT( pxListItem )	( ( pxListItem )->pxNext )

/* 获取列表项的尾部值 */
#define listGET_END_MARKER( pxList )	( ( ListItem_t const * ) ( &( ( pxList )->xListEnd ) ) )

/* 列表是否为空 */
#define listLIST_IS_EMPTY( pxList )	( ( BaseType_t ) ( ( pxList )->uxNumberOfItems == ( UBaseType_t ) 0 ) )

/* 列表长度 */
#define listCURRENT_LIST_LENGTH( pxList )	( ( pxList )->uxNumberOfItems )


/* 列表索引向后移1，并返回所有者 */
#define listGET_OWNER_OF_NEXT_ENTRY( pxTCB, pxList )										\
{																							\
	List_t * const pxConstList = ( pxList );												\
	( pxConstList )->pxIndex = ( pxConstList )->pxIndex->pxNext;							\
	if( ( void * ) ( pxConstList )->pxIndex == ( void * ) &( ( pxConstList )->xListEnd ) )	\
	{																						\
		( pxConstList )->pxIndex = ( pxConstList )->pxIndex->pxNext;						\
	}																						\
	( pxTCB ) = ( pxConstList )->pxIndex->pvOwner;											\
}


/* 返回列表head项的所有者 */
#define listGET_OWNER_OF_HEAD_ENTRY( pxList )  ( (&( ( pxList )->xListEnd ))->pxNext->pvOwner )

/* 某个列表项是否属于对应的列表 */
#define listIS_CONTAINED_WITHIN( pxList, pxListItem ) ( ( BaseType_t ) ( ( pxListItem )->pvContainer == ( void * ) ( pxList ) ) )

/* 列表项对应的列表 */
#define listLIST_ITEM_CONTAINER( pxListItem ) ( ( pxListItem )->pvContainer )

/* 检查列表是否已经被初始化 */
#define listLIST_IS_INITIALISED( pxList ) ( ( pxList )->xListEnd.xItemValue == portMAX_DELAY )

/* 初始化列表 */
void vListInitialise( List_t * const pxList ) PRIVILEGED_FUNCTION;

/* 初始化列表项 */
void vListInitialiseItem( ListItem_t * const pxItem ) PRIVILEGED_FUNCTION;

/* 将一个列表项插到列表中 */
void vListInsert( List_t * const pxList, ListItem_t * const pxNewListItem ) PRIVILEGED_FUNCTION;

/* 和上面有啥区别？ */
void vListInsertEnd( List_t * const pxList, ListItem_t * const pxNewListItem ) PRIVILEGED_FUNCTION;

/* 删除一个列表项，列表项里有指回列表的指针，所以只要一个参数就行了 */
UBaseType_t uxListRemove( ListItem_t * const pxItemToRemove ) PRIVILEGED_FUNCTION;

#ifdef __cplusplus
}
#endif

#endif

