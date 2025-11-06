#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h> 

// 定义储存结构
typedef enum { ELEMENT_NODE, TEXT_NODE } NodeType;

typedef struct HtmlNode {
    NodeType type;
    char* tagName;
    char* textContent;
    struct HtmlNode* parent;
    struct HtmlNode** children;
    int childCount;
    int capacity;
    int startPos;
    int endPos;
} HtmlNode;

typedef struct {
    HtmlNode** items;
    int top;
    int capacity;
} Stack;

typedef struct {
    HtmlNode** nodes;
    int count;
} NodeList;

// libcurl 保存下载数据的结构体
typedef struct {
    char* buffer;
    size_t size;
} MemoryStruct;


// 全局变量
char* G_htmlContent = NULL;  // 内容
HtmlNode* G_rootNode = NULL;
int G_parsingError = 0; // 标记是否有问题

//函数
void CheckHTML();
void OutHTML(const char* path);
void Text(const char* path);
void buildDOM();
int isVoidElement(const char* tag);
int isBlockTag(const char* tag);
int isWhitespace(const char* str);
void toLowerStr(char* str);
char* strndup_custom(const char* s, size_t n);
HtmlNode* createNode(NodeType type);
void addChild(HtmlNode* parent, HtmlNode* child);
void freeTree(HtmlNode* node);
Stack* createStack(int capacity);
void push(Stack* s, HtmlNode* node);
HtmlNode* pop(Stack* s);
HtmlNode* peek(Stack* s);
void freeStack(Stack* s);

// --- 新增 ---: 函数声明
char* readHtmlFromFile(const char* filename);
char* fetchHtmlFromUrl(const char* url);
static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp);


int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <filename.html or https://example.com>\n", argv[0]); // --- 修改 ---: 更新用法提示
        return 1;
    }

    const char* input = argv[1];

    // 检查输入是 URL 还是文件
    if (strncmp(input, "http://", 7) == 0 || strncmp(input, "https://", 8) == 0) {
        printf("Fetching from URL: %s\n", input);
        G_htmlContent = fetchHtmlFromUrl(input);
    } else {
        printf("Reading from file: %s\n", input);
        G_htmlContent = readHtmlFromFile(input);
    }

    // 检查内容是否成功加载
    if (!G_htmlContent) {
        printf("Error: Failed to get HTML content from '%s'.\n", input);
        return 1;
    }
    
    buildDOM();

    char command[1024];
    while (1) {
        printf("\nEnter command (CheckHTML, OutHTML(/path), Text(/path), Exit): ");
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }
        
        command[strcspn(command, "\r\n")] = 0;
        
        if (strcmp(command, "Exit") == 0) {
            break;
        }

        char path[1024] = {0};
        if (strcmp(command, "CheckHTML") == 0) {
            CheckHTML();
        } else if (sscanf(command, "OutHTML(%[^)])", path) == 1) {
            OutHTML(path);
        } else if (sscanf(command, "Text(%[^)])", path) == 1) {
            Text(path);
        } else {
            printf("Invalid command.\n");
        }
    }
    
    freeTree(G_rootNode);
    free(G_htmlContent); // G_htmlContent 现在由 readHtmlFromFile 或 fetchHtmlFromUrl 分配
    return 0;
}

// --- 新增 ---: libcurl 的写回调函数
static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    MemoryStruct* mem = (MemoryStruct*)userp;

    char* ptr = (char*)realloc(mem->buffer, mem->size + realsize + 1);
    if (ptr == NULL) {
        printf("Error: realloc failed in callback\n");
        return 0; // 返回 0 会使 libcurl 停止传输
    }

    mem->buffer = ptr;
    memcpy(&(mem->buffer[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->buffer[mem->size] = '\0';

    return realsize;
}

// 从 URL 获取 HTML 内容的函数！！！！
char* fetchHtmlFromUrl(const char* url) {
    CURL* curl;
    CURLcode res;
    MemoryStruct chunk;

    chunk.buffer = (char*)malloc(1); // 初始化
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback); // 设置写回调
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk); // 传递 chunk 结构体给回调
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0"); // 设置 User-Agent
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // 自动处理重定向
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L); // 验证 SSL 证书
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            free(chunk.buffer);
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return NULL;
        }

        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();

    return chunk.buffer; // 返回下载到的完整内容
}

// 从文件读取 HTML 内容的函数
char* readHtmlFromFile(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Cannot open file '%s'\n", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(fileSize + 1);
    if (!content) {
        printf("Error: Cannot allocate memory for file content.\n");
        fclose(file);
        return NULL;
    }
    fread(content, 1, fileSize, file);
    content[fileSize] = '\0';
    fclose(file);
    
    return content;
}


// 具体实现
void buildDOM() {
    // 初始化根节点
    G_rootNode = createNode(ELEMENT_NODE);
    G_rootNode->tagName = strdup("document_root");

    // 创建栈用于管理标签层级，根节点入栈
    Stack* stack = createStack(256);
    push(stack, G_rootNode);

    char* ptr = G_htmlContent; // 指向HTML内容的指针

    // 遍历HTML内容
    while (*ptr) {
        char* start = ptr;
        
        // 提取标签外的文本内容
        while (*ptr && *ptr != '<') {
            ptr++;
        }

        // 处理文本节点（非空白文本才添加）
        if (ptr > start) {
            char* text = strndup_custom(start, ptr - start);
            if (!isWhitespace(text)) {
                 HtmlNode* textNode = createNode(TEXT_NODE);
                 textNode->textContent = text;
                 addChild(peek(stack), textNode);
            } else {
                free(text);
            }
        }
        
        if (*ptr == '\0') break; // 内容结束则退出

        int tagStartPos = ptr - G_htmlContent; // 记录标签起始位置
        ptr++;

        // 处理注释标签（）
        if (*ptr == '!') {
            if (strncmp(ptr, "!--", 3) == 0) {
                ptr = strstr(ptr, "-->");
                if (ptr) ptr += 3; else break;
            } else {
                ptr = strchr(ptr, '>');
                if (ptr) ptr++; else break;
            }
            continue;
        }

        // 处理闭合标签（</tag>）
        if (*ptr == '/') {
            ptr++;
            char* tagNameStart = ptr;
            while (*ptr && *ptr != '>') ptr++; // 提取标签名
            
            char tempTag[100];
            size_t tagLen = ptr - tagNameStart;
            if (tagLen > 99) tagLen = 99;
            strncpy(tempTag, tagNameStart, tagLen);
            tempTag[tagLen] = '\0';
            toLowerStr(tempTag);
            
            // 检查标签匹配
            HtmlNode* topNode = peek(stack);
            if (topNode && topNode->tagName && strcmp(tempTag, topNode->tagName) == 0) {
                pop(stack)->endPos = (ptr - G_htmlContent) + 1;
            } else {
                if (topNode && topNode->tagName) {
                    printf("CheckHTML Error: Mismatched closing tag </%s>, expected </%s>\n", tempTag, topNode->tagName);
                } else {
                     printf("CheckHTML Error: Unexpected closing tag </%s>\n", tempTag);
                }
                G_parsingError = 1;
            }
            if (*ptr == '>') ptr++;
        } 
        // 处理开始标签（<tag>）
        else {
            char* tagNameStart = ptr;
            // 提取标签名（遇到空格、>、/停止）
            while (*ptr && !isspace((unsigned char)*ptr) && *ptr != '>' && *ptr != '/') {
                ptr++;
            }
            
            char tagName[100];
            size_t tagLen = ptr - tagNameStart;
            if (tagLen > 99) tagLen = 99;
            strncpy(tagName, tagNameStart, tagLen);
            tagName[tagLen] = '\0';
            toLowerStr(tagName);
            
            // 特殊处理script和style标签（直接找闭合标签）
            if (strcmp(tagName, "script") == 0 || strcmp(tagName, "style") == 0) {
                 char endTag[110];
                 sprintf(endTag, "</%s>", tagName);
                 ptr = strstr(ptr, endTag);
                 if (ptr) ptr += strlen(endTag); else break;
                 continue;
            }
            
            // 判断自闭合标签（/>结尾）
            char* tagEnd = ptr;
            while (*tagEnd && *tagEnd != '>') {
                tagEnd++;
            }

            int isSelfClosing = 0;
            if (tagEnd > ptr && *(tagEnd - 1) == '/') {
                isSelfClosing = 1;
            }
            
            ptr = tagEnd;
            if (*ptr == '>') ptr++;

            // 创建元素节点并添加到父节点
            HtmlNode* newNode = createNode(ELEMENT_NODE);
            newNode->tagName = strdup(tagName);
            newNode->startPos = tagStartPos;
            newNode->endPos = ptr - G_htmlContent;
            
            addChild(peek(stack), newNode);
            
            // 非自闭合且非空元素入栈（继续处理子节点）
            if (!isSelfClosing && !isVoidElement(newNode->tagName)) {
                push(stack, newNode);
            }
        }
    }

    // 检查未闭合标签
    if (stack->top > 0) {
        printf("CheckHTML Error: Unclosed tag <%s>\n", peek(stack)->tagName);
        G_parsingError = 1;
    }
    
    freeStack(stack); // 释放栈资源
}

void CheckHTML() {
    if (G_parsingError) {
        printf("CheckHTML: HTML is invalid.\n");
    } else {
        printf("CheckHTML: HTML is valid.\n");
    }
}

void findNodesRecursive(HtmlNode* node, char** pathParts, int depth, int pathLen, NodeList* result) {
    if (!node || node->type != ELEMENT_NODE) return;
    
    if (strcmp(node->tagName, pathParts[depth]) == 0) {
        if (depth == pathLen - 1) {
            result->nodes = (HtmlNode**)realloc(result->nodes, sizeof(HtmlNode*) * (result->count + 1));
            result->nodes[result->count++] = node;
        } else if (depth < pathLen - 1) {
            for (int i = 0; i < node->childCount; ++i) {
                findNodesRecursive(node->children[i], pathParts, depth + 1, pathLen, result);
            }
        }
    }
}

NodeList* findNodesByPath(const char* path) {
    char** pathParts = NULL;
    int pathLen = 0;
    char* pathCopy = strdup(path);
    char* token = strtok(pathCopy, "/");
    while(token) {
        pathParts = (char**)realloc(pathParts, sizeof(char*) * (pathLen + 1));
        pathParts[pathLen] = strdup(token);
        toLowerStr(pathParts[pathLen]);
        pathLen++;
        token = strtok(NULL, "/");
    }
    free(pathCopy);

    NodeList* result = (NodeList*)calloc(1, sizeof(NodeList));
    
    if (pathLen > 0) {
        for (int i = 0; i < G_rootNode->childCount; ++i) {
             findNodesRecursive(G_rootNode->children[i], pathParts, 0, pathLen, result);
        }
    }
    
    for (int i = 0; i < pathLen; ++i) free(pathParts[i]);
    free(pathParts);
    
    return result;
}

void OutHTML(const char* path) {
    NodeList* result = findNodesByPath(path);
    if(result->count == 0){
        printf("OutHTML: No elements found for path '%s'\n", path);
    }
    for(int i = 0; i < result->count; ++i) {
        HtmlNode* node = result->nodes[i];
        if(node->startPos != -1 && node->endPos != -1 && node->endPos > node->startPos){
            printf("%.*s\n", node->endPos - node->startPos, G_htmlContent + node->startPos);
        }
    }
    free(result->nodes);
    free(result);
}

void extractTextRecursive(HtmlNode* node, char* buffer, int* bufferLen, int capacity){
    if(!node || *bufferLen >= capacity - 1) return;

    if(node->type == TEXT_NODE && node->textContent){
        char* text = node->textContent;
        int inSpace = 0;
        for(int i = 0; text[i] && *bufferLen < capacity - 1; ++i){
            if(isspace((unsigned char)text[i])){
                if(!inSpace){
                    buffer[(*bufferLen)++] = ' ';
                    inSpace = 1;
                }
            } else {
                buffer[(*bufferLen)++] = text[i];
                inSpace = 0;
            }
        }
    } else if (node->type == ELEMENT_NODE){
        for(int i=0; i < node->childCount; ++i){
            extractTextRecursive(node->children[i], buffer, bufferLen, capacity);
        }
        if(isBlockTag(node->tagName)){
            if(*bufferLen > 0 && *bufferLen < capacity - 1 && buffer[*bufferLen - 1] != '\n'){
                buffer[(*bufferLen)++] = '\n';
            }
        }
    }
}

void Text(const char* path){
     NodeList* result = findNodesByPath(path);
     if(result->count == 0){
        printf("Text: No elements found for path '%s'\n", path);
    }
    for(int i = 0; i < result->count; ++i) {
        int capacity = 1024 * 10;
        char* buffer = (char*)calloc(capacity, sizeof(char));
        int len = 0;
        extractTextRecursive(result->nodes[i], buffer, &len, capacity);

        while(len > 0 && isspace((unsigned char)buffer[len-1])){
            len--;
        }
        buffer[len] = '\0';
        
        printf("%s\n", buffer);
        free(buffer);
    }
    free(result->nodes);
    free(result);
}

// 一些辅助声明函数
int isVoidElement(const char* tag) {
    const char* voidTags[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr", NULL
    };
    for (int i = 0; voidTags[i] != NULL; ++i) {
        if (strcmp(tag, voidTags[i]) == 0) return 1;
    }
    return 0;
}

int isBlockTag(const char* tag) {
    const char* blockTags[] = {
        "p", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "div", 
        "table", "tr", "form", "body", "html", NULL
    };
    for (int i = 0; blockTags[i] != NULL; ++i) {
        if (strcmp(tag, blockTags[i]) == 0) return 1;
    }
    return 0;
}

char* strndup_custom(const char* s, size_t n) {
    // ... (原有函数不变) ...
    char* p = (char*)malloc(n + 1);
    if (p) {
        strncpy(p, s, n);
        p[n] = '\0';
    }
    return p;
}

void toLowerStr(char* str) {
    // ... (原有函数不变) ...
    for (; *str; ++str) *str = tolower((unsigned char)*str);
}

int isWhitespace(const char* str) {
    // ... (原有函数不变) ...
    while (*str) {
        if (!isspace((unsigned char)*str)) return 0;
        str++;
    }
    return 1;
}

HtmlNode* createNode(NodeType type) {
    // ... (原有函数不变) ...
    HtmlNode* node = (HtmlNode*)calloc(1, sizeof(HtmlNode));
    node->type = type;
    return node;
}

void addChild(HtmlNode* parent, HtmlNode* child) {
    // ... (原有函数不变) ...
    if (parent->childCount >= parent->capacity) {
        parent->capacity = (parent->capacity == 0) ? 8 : parent->capacity * 2;
        parent->children = (HtmlNode**)realloc(parent->children, sizeof(HtmlNode*) * parent->capacity);
    }
    parent->children[parent->childCount++] = child;
    child->parent = parent;
}

void freeTree(HtmlNode* node) {
    // ... (原有函数不变) ...
    if (!node) return;
    for (int i = 0; i < node->childCount; ++i) {
        freeTree(node->children[i]);
    }
    free(node->tagName);
    free(node->textContent);
    free(node->children);
    free(node);
}

Stack* createStack(int capacity) {
    // ... (原有函数不变) ...
    Stack* s = (Stack*)malloc(sizeof(Stack));
    s->items = (HtmlNode**)malloc(sizeof(HtmlNode*) * capacity);
    s->top = -1;
    s->capacity = capacity;
    return s;
}

void push(Stack* s, HtmlNode* node) {
    // ... (原有函数不变) ...
    if (s->top < s->capacity - 1) {
        s->items[++s->top] = node;
    }
}

HtmlNode* pop(Stack* s) {
    // ... (原有函数不变) ...
    if (s->top > -1) {
        return s->items[s->top--];
    }
    return NULL;
}

HtmlNode* peek(Stack* s) 
{
    // ... (原有函数不变) ...
    if (s->top > -1) 
    {
        return s->items[s->top];
    }
    return NULL;
}

void freeStack(Stack* s) 
{
    // ... (原有函数不变) ...
    free(s->items);
    free(s);
}