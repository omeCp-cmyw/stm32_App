#include "tool_list.h"

/*
********************************************************************************
* define module variants
********************************************************************************
*/

/*******************************************************************
** 函数名	: LS_LIST_Init
** 函数描述	: 初始化链表。
** 参数		: [in] plist: 链表指针
** 返回		: 成功返回true，失败返回false
********************************************************************/
BOOLEAN LS_LIST_Init(LIST_T *plist)
{
    if (plist == 0) {
        return FALSE;
    }

    plist->head = 0;
    plist->tail = 0;
    plist->inum = 0;
    return TRUE;
}

/*******************************************************************
** 函数名	: LS_LIST_CreateList
** 函数描述	: 用一段静态内存创建链表，节点内存按nodesize步进划分。
** 参数		: [in] plist:    链表指针
**			: [in] nodeptr:  节点首地址
**			: [in] nodenum:  节点个数
**			: [in] nodesize: 节点大小（含NODE_T头）
** 返回		: 成功返回true，失败返回false
********************************************************************/
BOOLEAN LS_LIST_CreateList(LIST_T *plist, INT8U *nodeptr, INT32U nodenum, INT32U nodesize)
{
    if (!LS_LIST_Init(plist)) {
        return FALSE;
    }

    nodeptr += sizeof(NODE_T);
    for (; nodenum > 0; nodenum--) {
        if (!LS_LIST_AppendListEle(plist, nodeptr)) {
            return FALSE;
        }
        nodeptr += nodesize;
    }
    return TRUE;
}

/*******************************************************************
** 函数名	: LS_LIST_GetNodeNum
** 函数描述	: 获取链表节点个数。
** 参数		: [in] plist: 链表指针
** 返回		: 链表节点个数
********************************************************************/
INT32U LS_LIST_GetNodeNum(LIST_T *plist)
{
    if (plist == 0) {
        return 0;
    } else {
        return (plist->inum);
    }
}

/*******************************************************************
** 函数名	: LS_LIST_GetListHead
** 函数描述	: 获取链表头节点。
** 参数		: [in] plist: 链表指针
** 返回		: 返回头节点; 链表无节点则返回0
********************************************************************/
INT8U *LS_LIST_GetListHead(LIST_T *plist)
{
    if (plist == 0 || plist->inum == 0) {
        return 0;
    } else {
        return (((INT8U *)plist->head) + sizeof(NODE_T));
    }
}

/*******************************************************************
** 函数名	: LS_LIST_GetListTail
** 函数描述	: 获取链表尾节点。
** 参数		: [in] plist: 链表指针
** 返回		: 返回尾节点; 链表无节点则返回0
********************************************************************/
INT8U *LS_LIST_GetListTail(LIST_T *plist)
{
    if (plist == 0 || plist->inum == 0) {
        return 0;
    } else {
        return (((INT8U *)plist->tail) + sizeof(NODE_T));
    }
}

/*******************************************************************
** 函数名	: LS_LIST_GetNextEle
** 函数描述	: 获取指定节点的后一个节点。
** 参数		: [in] pnode: 链表当前节点
** 返回		: 返回后一节点指针; 无节点返回0
********************************************************************/
INT8U *LS_LIST_GetNextEle(INT8U *pnode)
{
    NODE_T *curnode;

    if (pnode == 0) {
        return 0;
    }

    curnode = (NODE_T *)(((INT8U *)pnode) - sizeof(NODE_T));
    if ((curnode = curnode->next) == 0) {
        return 0;
    } else {
        return (((INT8U *)curnode) + sizeof(NODE_T));
    }
}

/*******************************************************************
** 函数名	: LS_LIST_GetPrivEle
** 函数描述	: 获取指定节点的前一个节点。
** 参数		: [in] pnode: 链表当前节点
** 返回		: 返回前一节点指针; 无节点返回0
********************************************************************/
INT8U *LS_LIST_GetPrivEle(INT8U *pnode)
{
    NODE_T *curnode;

    if (pnode == 0) {
        return 0;
    }

    curnode = (NODE_T *)(((INT8U *)pnode) - sizeof(NODE_T));
    if ((curnode = curnode->priv) == 0) {
        return 0;
    } else {
        return (((INT8U *)curnode) + sizeof(NODE_T));
    }
}

/*******************************************************************
** 函数名	: LS_LIST_AppendListEle
** 函数描述	: 将新节点挂接到链表尾。
** 参数		: [in] plist: 链表指针
**			: [in] pnode: 需要接进节点
** 返回		: 成功返回true，失败返回false
********************************************************************/
BOOLEAN LS_LIST_AppendListEle(LIST_T *plist, INT8U *pnode)
{
    NODE_T *curnode;

    if (plist == 0 || pnode == 0) {
        return FALSE;
    }

    curnode = (NODE_T *)(((INT8U *)pnode) - sizeof(NODE_T));
    curnode->priv = plist->tail;
    if (plist->inum == 0) {
        plist->head = curnode;
    } else {
        plist->tail->next = curnode;
    }
    curnode->next = 0;
    plist->tail = curnode;
    plist->inum++;
    return TRUE;
}

/*******************************************************************
** 函数名	: LS_LIST_InsertListHead
** 函数描述	: 将新节点插入到链表头。
** 参数		: [in] plist: 链表指针
**			: [in] pnode: 插入节点
** 返回		: 成功返回true，失败返回false
********************************************************************/
BOOLEAN LS_LIST_InsertListHead(LIST_T *plist, INT8U *pnode)
{
    NODE_T *curnode;

    if (plist == 0 || pnode == 0) {
        return FALSE;
    }

    curnode = (NODE_T *)(((INT8U *)pnode) - sizeof(NODE_T));
    curnode->next = plist->head;
    if (plist->inum == 0) {
        plist->tail = curnode;
    } else {
        plist->head->priv = curnode;
    }
    curnode->priv = 0;
    plist->head = curnode;
    plist->inum++;
    return TRUE;
}

/*******************************************************************
** 函数名	: LS_LIST_InsertNodeBefore
** 函数描述	: 在指定节点前插入一个新节点。
** 参数		: [in] plist: 链表指针
**			: [in] cnode: 指定节点
**			: [in] dnode: 插入节点
** 返回		: 成功返回true，失败返回false
********************************************************************/
BOOLEAN LS_LIST_InsertNodeBefore(LIST_T *plist, INT8U *cnode, INT8U *dnode)
{
    NODE_T *curnode, *insnode;

    if (plist == 0 || cnode == 0 || dnode == 0) {
        return FALSE;
    }

    if (plist->inum == 0) {
        return FALSE;
    }

    curnode = (NODE_T *)(((INT8U *)cnode) - sizeof(NODE_T));
    insnode = (NODE_T *)(((INT8U *)dnode) - sizeof(NODE_T));

    insnode->next = curnode;
    insnode->priv = curnode->priv;
    if (curnode->priv == 0) {
        plist->head = insnode;
    } else {
        curnode->priv->next = insnode;
    }
    curnode->priv = insnode;
    plist->inum++;
    return TRUE;
}

/*******************************************************************
** 函数名	: LS_LIST_InsertNodeAfter
** 函数描述	: 在指定节点后插入一个新节点。
** 参数		: [in] plist: 链表指针
**			: [in] cnode: 指定节点
**			: [in] dnode: 插入节点
** 返回		: 成功返回true，失败返回false
********************************************************************/
BOOLEAN LS_LIST_InsertNodeAfter(LIST_T *plist, INT8U *cnode, INT8U *dnode)
{
    NODE_T *curnode, *insnode;

    if (plist == 0 || cnode == 0 || dnode == 0) {
        return FALSE;
    }

    if (plist->inum == 0) {
        return FALSE;
    }

    curnode = (NODE_T *)(((INT8U *)cnode) - sizeof(NODE_T));
    insnode = (NODE_T *)(((INT8U *)dnode) - sizeof(NODE_T));

    insnode->next = curnode->next;
    insnode->priv = curnode;
    if (curnode->next == 0) {
        plist->tail = insnode;
    } else {
        curnode->next->priv = insnode;
    }
    curnode->next = insnode;
    plist->inum++;
    return TRUE;
}

/*******************************************************************
** 函数名	: LS_LIST_DeleListEle
** 函数描述	: 删除指定节点，并将后一节点返回。
** 参数		: [in] plist: 链表指针
**			: [in] pnode: 链表当前节点
** 返回		: 返回后一节点指针; 无节点返回0
********************************************************************/
INT8U *LS_LIST_DeleListEle(LIST_T *plist, INT8U *pnode)
{
    NODE_T *curnode, *privnode, *nextnode;

    if (plist == 0 || pnode == 0) {
        return 0;
    }

    if (plist->inum == 0) {
        return 0;
    }

    plist->inum--;
    curnode  = (NODE_T *)(((INT8U *)pnode) - sizeof(NODE_T));
    privnode = curnode->priv;
    nextnode = curnode->next;
    if (privnode == 0) {
        plist->head = nextnode;
    } else {
        privnode->next = nextnode;
    }

    if (nextnode == 0) {
        plist->tail = privnode;
        return 0;
    } else {
        nextnode->priv = privnode;
        return (((INT8U *)nextnode) + sizeof(NODE_T));
    }
}

/*******************************************************************
** 函数名	: LS_LIST_DeleListHead
** 函数描述	: 删除链表头节点，返回原头节点指针。
** 参数		: [in] plist: 链表指针
** 返回		: 返回链表头节点指针; 无节点返回0
********************************************************************/
INT8U *LS_LIST_DeleListHead(LIST_T *plist)
{
    INT8U *pnode;

    if (plist == 0 || plist->inum == 0) {
        return 0;
    }

    pnode = ((INT8U *)plist->head) + sizeof(NODE_T);
    LS_LIST_DeleListEle(plist, pnode);
    return pnode;
}

/*******************************************************************
** 函数名	: LS_LIST_DeleListTail
** 函数描述	: 删除链表尾节点，返回原尾节点指针。
** 参数		: [in] plist: 链表指针
** 返回		: 返回链表尾节点指针; 无节点返回0
********************************************************************/
INT8U *LS_LIST_DeleListTail(LIST_T *plist)
{
    INT8U *pnode;

    if (plist == 0 || plist->inum == 0) {
        return 0;
    }

    pnode = (INT8U *)plist->tail + sizeof(NODE_T);
    LS_LIST_DeleListEle(plist, pnode);
    return pnode;
}

/*******************************************************************
** 函数名	: LS_LIST_CheckError
** 函数描述	: 检查链表是否有效（节点均在合法内存区间内）。
** 参数		: [in] plist: 链表指针
**			: [in] bptr:  最小地址
**			: [in] eptr:  最大地址
** 返回		: 有效返回true，无效返回false
********************************************************************/
BOOLEAN LS_LIST_CheckError(LIST_T *plist, void *bptr, void *eptr)
{
    INT32U  count;
    NODE_T *curnode;

    if (plist == 0) {
        return FALSE;
    }

    count = 0;
    curnode = plist->head;
    while (curnode != 0) {
        if (((void *)curnode < bptr) || ((void *)curnode > eptr)) {
            return FALSE;
        }

        if (++count > plist->inum) {
            return FALSE;
        }
        curnode = curnode->next;
    }

    if (count != plist->inum) {
        return FALSE;
    }

    count = 0;
    curnode = plist->tail;
    while (curnode != 0) {
        if (((void *)curnode < bptr) || ((void *)curnode > eptr)) {
            return FALSE;
        }

        if (++count > plist->inum) {
            return FALSE;
        }
        curnode = curnode->priv;
    }

    if (count != plist->inum) {
        return FALSE;
    }

    return TRUE;
}
