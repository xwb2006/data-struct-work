#include<stdio.h>
#include <stdlib.h>

typedef int Status;
typedef int ElemType;
#define OK 1
#define ERROR 0
#define OVERFLOW -2 


// 线性表的动态分配顺序存储结构
#define LIST_INIT_SIZE 100 // 线性表存储空间的初始分配量
#define LISTINCREMENT 10  // 线性表存储空间的分配增量
typedef struct {
    ElemType *elem;        // 存储空间基址
    int length;            // 当前长度
    int listsize;          // 当前分配的存储容量(以 sizeof(ElemType) 为单位)
} SqList;

Status InitList_Sq(SqList &L) {
    // 构造一个空的线性表 L。
    L.elem = (ElemType *)malloc(LIST_INIT_SIZE * sizeof(ElemType));
    if (!L.elem) exit(OVERFLOW); // 存储分配失败
    L.length = 0;                // 空表长度为 0
    L.listsize = LIST_INIT_SIZE; // 初始存储容量
    return OK;
}// InitList_Sq


Status ListInsert_Sq(SqList &L, int i, ElemType e) {
    // 在顺序线性表 L 中第 i 个位置之前插入新的元素 e，
    // i 的合法值为 1≤i≤ListLength_Sq(L) + 1
    if (i < 1 || i > L.length + 1) return ERROR; // i 值不合法
    if (L.length >= L.listsize) { // 当前存储空间已满，增加分配
        ElemType *newbase; // 调整指针声明位置
        newbase = (ElemType *)realloc(L.elem,
            (L.listsize + LISTINCREMENT) * sizeof(ElemType));
        if (!newbase) exit(OVERFLOW); // 存储分配失败
        L.elem = newbase; // 新基址
        L.listsize += LISTINCREMENT; // 增加存储容量
    }
    ElemType *q,*p;
    q = &(L.elem[i - 1]); // q 为插入位置
    for (p = &(L.elem[L.length - 1]); p >= q; --p) *(p + 1) = *p;
    // 插入位置及之后的元素右移
    *q = e; // 插入 e
    ++L.length; // 表长增 1
    return OK;
}// ListInsert_Sq


Status ListDelete_Sq(SqList &L, int i, ElemType &e) {
    // 在顺序线性表 L 中删除第 i 个元素，并用 e 返回其值
    // i 的合法值为 1≤i≤ListLength_Sq(L)
    if ((i < 1) || (i > L.length)) return ERROR; // i 值不合法
    ElemType *p, *q; // 补充指针声明
    p = &(L.elem[i - 1]); // p 指向被删除元素
    e = *p; // 被删除元素的值赋给 e
    q = L.elem + L.length - 1; // 表尾元素的位置
    for (++p; p <= q; ++p) *(p - 1) = *p; // 被删除元素之后的元素左移
    --L.length; // 表长减 1
    return OK;
}// ListDelete_Sq


int LocateElem_Sq(SqList L, ElemType e, Status (*compare)(ElemType, ElemType)) {
    // 在顺序线性表 L 中查找第 1 个值与 e 满足 compare() 的元素的位序
    // 若找到，则返回其在 L 中的位序，否则返回 0
    int i = 1; // i 的初值为第 1 个元素的位序
    ElemType *p = L.elem; // p 的初值为第 1 个元素的存储位置
    while (i <= L.length && !(*compare)(*p++, e)) ++i;
    if (i <= L.length) return i;
    else return 0;
}// LocateElem_Sq

// 补充比较函数实现
Status equal(ElemType a, ElemType b) {
    return a == b ? OK : ERROR;
}

// 补充List结构体定义及相关函数声明（适配union函数）
typedef struct {
    ElemType *elem;
    int len;
    int listsize;
} List;

int ListLength(List L) {
    return L.len;
}

Status GetElem(List L, int i, ElemType &e) {
    if (i < 1 || i > L.len) return ERROR;
    e = L.elem[i - 1];
    return OK;
}

Status ListInsert(List &L, int i, ElemType e) {
    if (i < 1 || i > L.len + 1) return ERROR;
    if (L.len >= L.listsize) {
        ElemType *newbase = (ElemType *)realloc(L.elem, (L.listsize + LISTINCREMENT) * sizeof(ElemType));
        if (!newbase) exit(OVERFLOW);
        L.elem = newbase;
        L.listsize += LISTINCREMENT;
    }
    for (int j = L.len; j >= i; j--) {
        L.elem[j] = L.elem[j - 1];
    }
    L.elem[i - 1] = e;
    L.len++;
    return OK;
}

int LocateElem(List L, ElemType e, Status (*compare)(ElemType, ElemType)) {
    for (int i = 0; i < L.len; i++) {
        if (compare(L.elem[i], e)) {
            return i + 1; // 返回位序（1-based）
        }
    }
    return 0;
}

void union_List(List &La, List Lb) { // 重命名避免与关键字冲突
    // 将所有在线性表 Lb 中但不在 La 中的数据元素插入到 La 中
    int La_len = ListLength(La);
    int Lb_len = ListLength(Lb); // 求线性表的长度
    ElemType e;
    int i; // 补充循环变量声明
    for (i = 1; i <= Lb_len; i++) {
        GetElem(Lb, i, e); // 取 Lb 中第 i 个数据元素赋给 e
        if (!LocateElem(La, e, equal)) {
            ListInsert(La, ++La_len, e);
        }
        // La 中不存在和 e 相同的数据元素，则插入之
    }
}// union_List


//时间复杂度为O(La.length * Lb.length)
void MergeList(List La, List Lb, List &Lc) {
    // 已知线性表 La 和 Lb 中的数据元素按值非递减排列。
    // 归并 La 和 Lb 得到新的线性表 Lc，Lc 的数据元素也按值非递减排列。
    InitList_Sq((SqList &)Lc); // 假设List与SqList结构兼容，通过初始化函数初始化Lc
    int i = 1, j = 1, k = 0; // 补充变量声明
    ElemType ai, bj; // 补充变量声明
    int La_len = ListLength(La);
    int Lb_len = ListLength(Lb);
    while ((i <= La_len) && (j <= Lb_len)) { // La 和 Lb 均非空
        GetElem(La, i, ai);
        GetElem(Lb, j, bj);
        if (ai <= bj) {
            ListInsert(Lc, ++k, ai);
            ++i;
        } else {
            ListInsert(Lc, ++k, bj);
            ++j;
        }
    }
    while (i <= La_len) {
        GetElem(La, i++, ai);
        ListInsert(Lc, ++k, ai);
    }
    while (j <= Lb_len) {
        GetElem(Lb, j++, bj);
        ListInsert(Lc, ++k, bj);
    }
}// MergeList


//时间复杂度为O(La.length + Lb.length)
void MergeList_Sq(SqList La, SqList Lb, SqList &Lc) {
    // 已知顺序线性表 La 和 Lb 的元素按值非递减排列
    // 归并 La 和 Lb 得到新的顺序线性表 Lc，Lc 的元素也按值非递减排列
    ElemType *pa, *pb, *pc, *pa_last, *pb_last;
    pa = La.elem;
    pb = Lb.elem;
    Lc.listsize = Lc.length = La.length + Lb.length;
    pc = Lc.elem = (ElemType *)malloc(Lc.listsize * sizeof(ElemType));
    if (!Lc.elem) exit(OVERFLOW); // 存储分配失败
    pa_last = La.elem + La.length - 1;
    pb_last = Lb.elem + Lb.length - 1;
    while (pa <= pa_last && pb <= pb_last) { // 归并
        if (*pa <= *pb) *pc++ = *pa++;
        else *pc++ = *pb++;
    }
    while (pa <= pa_last) *pc++ = *pa++; // 插入 La 的剩余元素
    while (pb <= pb_last) *pc++ = *pb++; // 插入 Lb 的剩余元素
} // MergeList_Sq




/* 线性表的单链表存储结构*/
typedef struct LNode {
    ElemType data;
    struct LNode *next;
} LNode, *LinkList;

Status GetElem_L(LinkList L, int i, ElemType &e) {
    // L 为带头结点的单链表的头指针。
    // 当第 i 个元素存在时，其值赋给 e 并返回 OK，否则返回 ERROR
    LinkList p = L->next;
    int j = 1; // 初始化，p 指向第一个结点，j 为计数器
    while (p && j < i) { // 顺指针向后查找，直到 p 指向第 i 个元素或 p 为空
        p = p->next;
        ++j;
    }
    if (!p || j > i) return ERROR; // 第 i 个元素不存在
    e = p->data; // 取第 i 个元素
    return OK;
}// GetElem_L


int main() {
    // 示例代码逻辑
    SqList La, Lb, Lc;
    InitList_Sq(La);
    InitList_Sq(Lb);
    
    // 可以在这里添加测试代码
    
    return 0;
}