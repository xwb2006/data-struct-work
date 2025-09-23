#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// 链表中每个单词结构
typedef struct Node{
    char *word;       // 储存单词
    struct Node *next; // 指向下一个
} Node;

// 链表结构
typedef struct{
    Node *head;       // 头元素
    int length;       // 记录长度
} List;

// 创建链表 
List* initList(){
    List *list = (List*)malloc(sizeof(List));
    list->head = (Node*)malloc(sizeof(Node));
    list->head->word = NULL;
    list->head->next = NULL;
    list->length = 0;
    return list;      // 因为是动态分配，需要返回地址指针
}

// 创建新节点
Node* AddNode(char *word)
{
    Node *node = (Node*)malloc(sizeof(Node));
    node->word = (char*)malloc(strlen(word) + 1); 
    strcpy(node->word, word);
    node->next = NULL;
    return node;
}

// 打印列表
void printwords(List *list)
{
    Node *cur = list->head->next;
    while(cur != NULL)
    {
        printf("%s ", cur->word);
        cur = cur->next;
    }
    printf("\n");
}

// 清空链表
void clearlist(List *list)
{
    Node *cur = list->head->next;
    Node *temp;
    while(cur != NULL)
    {
        temp = cur;
        cur = cur->next;
        free(temp->word);
        free(temp);
    }
    list->head->next = NULL;  // 清空后重置头节点的next
    list->length = 0;         // 重置长度
}

// 处理输入文本
void input_text(List *list, const char *text)
{
    clearlist(list); // 先清空
    char *words = (char*)malloc(strlen(text) + 1);
    strcpy(words, text); // 复制过来，避免直接操作原句

    char *word = strtok(words, " "); // 获取头部单词
    Node *cur = list->head;
    while(word != NULL)
    {
        Node *node = AddNode(word);
        cur->next = node;   // 加入链表
        cur = node;         // 更换现在的节点
        list->length++;
        word = strtok(NULL, " ");
    }
    free(words);
    printf("这是现在的句子:\n");
    printwords(list);
}

// 插入单词/句子
void insertwords(List *list, int pos, const char *text)
{
    if(pos < 0 || pos > list->length)
    {
        printf("输入位置错误\n");
        return;
    }
    // 指到前一个位置
    Node *cur = list->head;
    for(int i = 0; i < pos; i++)
    {
        cur = cur->next;
    }
    
    char *insertWords = (char*)malloc(strlen(text) + 1);
    strcpy(insertWords, text); 
    char *word = strtok(insertWords, " "); 

    while(word != NULL)
    {
        Node *node = AddNode(word);
        node->next = cur->next;  // 下面承接原来的指针
        cur->next = node;     // 修正：将当前节点的next指向新节点
        cur = node;     // 移动到新插入的节点
        list->length++;
        word = strtok(NULL, " ");
    }
    free(insertWords);
    printf("插入成功，这是现在的句子:\n"); 
    printwords(list);
}

void deleteWords(List *list, int pos, int count)
{
    if(pos < 0 || pos >= list->length)
    {
        printf("输入位置不对！\n"); 
        return;
    }
    if(count <= 0 || pos + count > list->length)
    {
        printf("删除数量不对！（可能越界了）\n"); 
        return;     
    }
    // 指到前一个位置
    Node *cur = list->head;
    for(int i = 0; i < pos; i++)
    {
        cur = cur->next;
    }
    for(int i = 0; i < count; i++)
    {
        Node *temp = cur->next;
        if(temp == NULL) break;
        cur->next = temp->next;
        free(temp->word);
        free(temp);
        list->length--;
    }
    printf("删除成功，这是现在的片段\n");
    printwords(list);
}

// 反转，修改next指向它的前一个结点
void reversewords(List *list)
{
    if(list->length <= 1) return;

    Node *pre = NULL;
    Node *cur = list->head->next;
    Node *next = NULL;

    while(cur != NULL)
    {
        next = cur->next;  // 先存档，防止后面找不到
        cur->next = pre;  // 核心：反转指针
        pre = cur;     // 向后移动
        cur = next;
    }
    list->head->next = pre;
    printf("倒置完成，现在的列表：\n");
    printwords(list);
}

// 判断是否回文
bool is_huiwen(List *list)
{
    if(list->length <= 1) return true; 
    
    // 读取所有单词，方便比较
    char **words = (char**)malloc(list->length * sizeof(char*));
    Node *cur = list->head->next;
    for(int i = 0; i < list->length; i++)
    {
        words[i] = cur->word; 
        cur = cur->next;
    }

    // 判断
    int l = 0, r = list->length - 1;
    while(l < r)
    {
        if(strcmp(words[l], words[r]) != 0) 
        {
            free(words);
            return false;
        }
        l++; 
        r--;
    }
    free(words);
    return true;
}

int countwords(List *list)
{
    return list->length;
}

// 查找单词（区分大小写）
int findword(List *list, char *word)
{
    Node *cur = list->head->next;
    int pos = 0;
    while (cur != NULL) {
        if (strcmp(cur->word, word) == 0) {
            return pos;
        }
        cur = cur->next;
        pos++;
    }
    return -1;  // 没找到
}

// 销毁链表
void destroyList(List *list)
{
    clearlist(list);
    free(list->head);
    printf("列表已销毁！\n"); 
}

void wordscount(List *list)
{
    if(list->length == 0)
    {
        printf("孩子，列表没东西啊！！！\n"); 
        return;
    }
    // 遍历
    char **setwords = (char**)malloc(list->length * sizeof(char*));
    int *counts = (int*)calloc(list->length, sizeof(int));
    int setlength = 0;
    
    Node *cur = list->head->next;
    while (cur != NULL) 
    {
        // 检查当前单词是否已在setwords中
        int found = -1;
        for (int i = 0; i < setlength; i++) {
            if (strcmp(setwords[i], cur->word) == 0) 
            {
                found = i;
                break;
            }
        }
        
        if (found != -1) 
        {
            counts[found]++; // 如果在，加1
        }
        else{
            setwords[setlength] = cur->word;
            counts[setlength] = 1;
            setlength++;
        }
        cur = cur->next;
    }
    
    // 打印统计结果
    printf("单词统计结果：\n");
    for (int i = 0; i < setlength; i++) {
        printf("%s 出现 %d 次\n", setwords[i], counts[i]);
    }
    free(setwords);
    free(counts);
}

int main() {
    printf("=== 单词单链表操作程序 ===\n");
    List *list = initList(); 
    
    while (true) {
        printf("---------菜单---------\n");
        printf("1.输入/更新文本  2.打印单词列表\n");
        printf("3.插入单词       4.删除单词\n");
        printf("5.倒置单词顺序   6.判断是否回文\n");
        printf("7.统计单词总数   8.查找单词（大小写敏感）\n");
        printf("9.销毁当前单词列表  10. 统计每个单词的出现次数\n");
        printf("0. 退出程序\n"); 

        int cmd;
        printf("请输入命令数字:");
        scanf("%d", &cmd);
        getchar();  // 吸收换行符
        
        switch (cmd) {
            case 1: {
                char text[1000];
                printf("请输入文本（单词按空格分隔）：");
                fgets(text, sizeof(text), stdin);
                text[strcspn(text, "\n")] = '\0';
                input_text(list, text);
                printf("\n"); 
                break;
            }
            case 2:
                printf("当前单词列表：");
                printwords(list);
                printf("\n"); 
                break;
            case 3: {
                int pos;
                char words[1000];
                printf("请输入插入位置（从0开始）：");
                scanf("%d", &pos);
                getchar();  // 吸收换行符
                printf("请输入要插入的单词（以空格分隔）：");
                fgets(words, sizeof(words), stdin);
                words[strcspn(words, "\n")] = '\0';
                insertwords(list, pos, words);
                printf("\n"); 
                break;
            }
            case 4: {
                int pos, count=1;
                printf("请输入删除起始位置（从0开始）：");
                scanf("%d", &pos);
                printf("请输入删除个数（默认1个）：");
                scanf("%d", &count);
                if (count < 0) count = 1;  // 确保至少删除一个
                deleteWords(list, pos, count); 
                printf("\n"); 
                break;
            }
            case 5:
                reversewords(list);
                printf("\n");
                break;
            case 6:
                if (is_huiwen(list)) {
                    printf("是「词级别回文」！\n");
                } else {
                    printf("不是「词级别回文」！\n");
                }
                break;
            case 7:
                printf("单词总数：%d\n", countwords(list));
                break;
            case 8: {
                char word[100];
                printf("请输入要查找的单词：");
                fgets(word, sizeof(word), stdin);
                word[strcspn(word, "\n")] = '\0';
                int pos = findword(list, word);
                if (pos != -1) {
                    printf("找到单词，位置：%d\n", pos);
                } else {
                    printf("未找到该单词\n");
                }
                break;
            }
            case 9:
                destroyList(list);
                printf("\n");
                break;
            case 10:
                wordscount(list);
                printf("\n"); 
                break;
            case 0:
                destroyList(list);
                free(list);  // 释放链表
                printf("程序退出，再见～\n");
                return 0;
            default:
                printf("命令无效，请重新输入！\n");
        }
    }
}