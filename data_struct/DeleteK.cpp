#include<stdio.h>
#include <stdlib.h>


typedef int Status;
typedef int ElemType;
#define OK 1
#define ERROR 0
#define OVERFLOW -2 
#define MAXSIZE 10

// 定义顺序表结构体（SqList）
typedef struct {
    ElemType data[MAXSIZE];  // 存储元素的数组
    int length;              // 当前长度
} SqList;

// 参数改为结构体变量（会产生副本）
Status DeleteK(SqList a, int i, int k) {
    // 此时可以直接用.操作符
    if (i < 1 || k < 1 || i + k - 1 > a.length) {
        printf("输入不合理！\n");
        return ERROR;
    }
    
    for (int j = i + k - 1; j < a.length; j++) {
        a.data[j - k] = a.data[j];
    }
    
    a.length -= k;
    
    for (int i = 0; i < a.length; i++) {
        printf("%d ", a.data[i]);
    }
    return OK;
}

int main() {
    SqList a;
    for (int i = 0; i < 8; i++) {
        a.data[i] = i + 1;
    }
    a.length = 8;
    
    int start = 2; 
    int k = 3;    
    
    DeleteK(a, start, k);
    
    return 0;
}
