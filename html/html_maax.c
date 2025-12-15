#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

// 定义节点类型
typedef enum { ELEMENT_NODE, TEXT_NODE } NodeType;

// 内容属性
typedef struct Attribute {
    char* key;
    char* value;
    struct Attribute* next;
} Attribute;

// 单个节点
typedef struct HtmlNode {
    NodeType type;
    char* tagName;
    char* textContent;
    Attribute* attributes;
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

// 结果输出调用
typedef struct {
    char* buffer;
    size_t size;
} MemoryStruct;

// 全局变量
char* G_htmlContent = NULL;
HtmlNode* G_rootNode = NULL;
NodeList* G_lastResult = NULL; // 储存last query，方便后续使用索引
int G_parsingError = 0;

// Core DOM
void buildDOM();
HtmlNode* createNode(NodeType type);
void addChild(HtmlNode* parent, HtmlNode* child);
void addAttribute(HtmlNode* node, const char* key, const char* value);
char* getAttribute(HtmlNode* node, const char* key);
void freeTree(HtmlNode* node);

// Networking/File
char* readHtmlFromFile(const char* filename);
char* fetchHtmlFromUrl(const char* url);
static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp);

// CSS Selector Engine
NodeList* querySelectorAll(HtmlNode* root, const char* selector);
void queryRecursive(HtmlNode* root, const char* selector, NodeList* results);
int matchNode(HtmlNode* node, const char* simpleSelector);

// Output/Interaction
void printNodeList(NodeList* list);
char* getOuterHTML(HtmlNode* node);
char* getInnerText(HtmlNode* node);

// Helpers
int isVoidElement(const char* tag);
int isBlockTag(const char* tag);
int isWhitespace(const char* str);
void toLowerStr(char* str);
char* strndup_custom(const char* s, size_t n);
char* trimWhitespace(char* str);

// Stack
Stack* createStack(int capacity);
void push(Stack* s, HtmlNode* node);
HtmlNode* pop(Stack* s);
HtmlNode* peek(Stack* s);
void freeStack(Stack* s);


int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <filename.html or https://example.com>\n", argv[0]);
        return 1;
    }

    const char* input = argv[1];

    if (strncmp(input, "http://", 7) == 0 || strncmp(input, "https://", 8) == 0) {
        printf("Fetching from URL: %s\n", input);
        G_htmlContent = fetchHtmlFromUrl(input);
    } else {
        printf("Reading from file: %s\n", input);
        G_htmlContent = readHtmlFromFile(input);
    }

    if (!G_htmlContent) {
        return 1;
    }

    buildDOM();
    if(G_parsingError) printf("OOOOi: HTML 出了点问题, DOM 可能不完整了.\n");

    printf("\nHTML 处理完毕. Enter commands.\n");
    printf("\n");
    printf("  query div.content     (CSS Selector)\n");
    printf("  Out[0].innerText      (Get text)\n");
    printf("  Out[0].outerHTML      (Get HTML)\n");
    printf("  Out[0].href           (Get href)\n");
    printf("  Exit\n");

    char command[1024];
    while (1) {
        printf("\n>> ");
        if (fgets(command, sizeof(command), stdin) == NULL) break;
        command[strcspn(command, "\r\n")] = 0;

        if (strcmp(command, "Exit") == 0 || strcmp(command, "exit") == 0) break;

        if (strncmp(command, "query ", 6) == 0) {
            const char* selector = command + 6;
            if (G_lastResult) {
                free(G_lastResult->nodes);
                free(G_lastResult);
            }
            G_lastResult = querySelectorAll(G_rootNode, selector);
            printNodeList(G_lastResult);
        }
        else if (strncmp(command, "Out[", 4) == 0) {
            int index;
            char buffer[256]; // 暂存属性名或子选择器

            // 1: 处理 Out[k].query(selector)
            // 注意 format 字符串：Out[%d].query(%[^)]) 意思是匹配 "Out[数字].query(字符串内容)"
            if (sscanf(command, "Out[%d].query(%[^)])", &index, buffer) == 2) {
                if (!G_lastResult || index < 0 || index >= G_lastResult->count) {
                    printf("Error: Index %d out of bounds (Count: %d)\n", index, G_lastResult ? G_lastResult->count : 0);
                } else {
                    // 1. 获取第 k 个节点作为新的根
                    HtmlNode* subRoot = G_lastResult->nodes[index];
                    printf(">> Querying inside node <%s> with selector: %s\n", subRoot->tagName, buffer);

                    // 2. 执行新的查询
                    NodeList* newResult = querySelectorAll(subRoot, buffer);

                    // 3. 释放旧的查询结果列表（注意：只释放列表容器，不释放节点本身，因为节点属于 DOM 树）
                    if (G_lastResult) {
                        free(G_lastResult->nodes);
                        free(G_lastResult);
                    }

                    // 4. 更新全局结果并打印
                    G_lastResult = newResult;
                    printNodeList(G_lastResult);
                }
            }/////////////看到这里了
            // 情况 2: 处理 Out[k].property (如 innerText, outerHTML, href)
            else if (sscanf(command, "Out[%d].%s", &index, buffer) == 2) {
                if (!G_lastResult || index < 0 || index >= G_lastResult->count) {
                    printf("Error: Index %d out of bounds (Count: %d)\n", index, G_lastResult ? G_lastResult->count : 0);
                } else {
                    HtmlNode* node = G_lastResult->nodes[index];
                    if (strcmp(buffer, "innerText") == 0) {
                        char* text = getInnerText(node);
                        printf("%s\n", text);
                        free(text);
                    } else if (strcmp(buffer, "outerHTML") == 0) {
                        char* html = getOuterHTML(node);
                        printf("%s\n", html);
                        free(html);
                    } else if (strcmp(buffer, "href") == 0) {
                        char* href = getAttribute(node, "href");
                        printf("%s\n", href ? href : "(null)");
                    } else if (strcmp(buffer, "tagName") == 0) {
                        printf("%s\n", node->tagName);
                    } else {
                        printf("Unknown property: %s\n", buffer);
                    }
                }
            } else {
                printf("Invalid format. Usage: Out[k].query(selector) or Out[k].property\n");
            }
        } else {
            printf("Unknown command: %s\n", command);
        }
    }

    if(G_lastResult) { free(G_lastResult->nodes); free(G_lastResult); }
    freeTree(G_rootNode);
    free(G_htmlContent);
    return 0;
}

// --- Attribute & Parsing Implementation ---

void addAttribute(HtmlNode* node, const char* key, const char* value) {
    Attribute* attr = (Attribute*)malloc(sizeof(Attribute));
    attr->key = strdup(key);
    toLowerStr(attr->key); // Store keys in lowercase
    attr->value = value ? strdup(value) : NULL;
    attr->next = node->attributes;
    node->attributes = attr;
}

char* getAttribute(HtmlNode* node, const char* key) {
    if (!node) return NULL;
    Attribute* cur = node->attributes;
    while (cur) {
        if (strcmp(cur->key, key) == 0) return cur->value;
        cur = cur->next;
    }
    return NULL;
}

void buildDOM() {
    G_rootNode = createNode(ELEMENT_NODE);
    G_rootNode->tagName = strdup("document_root");

    Stack* stack = createStack(256);
    push(stack, G_rootNode);

    char* ptr = G_htmlContent;

    while (*ptr) {
        char* start = ptr;
        
        // Find next tag start
        while (*ptr && *ptr != '<') ptr++;

        // Add text node
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
        
        if (*ptr == '\0') break;

        int tagStartPos = ptr - G_htmlContent;
        ptr++; // Skip '<'

        // Comments
        if (*ptr == '!') {
            if (strncmp(ptr, "!--", 3) == 0) {
                ptr = strstr(ptr, "-->");
                if (ptr) ptr += 3; else break;
            } else { // DOCTYPE etc
                ptr = strchr(ptr, '>');
                if (ptr) ptr++; else break;
            }
            continue;
        }

        // Closing Tag
        if (*ptr == '/') {
            ptr++;
            char* tagNameStart = ptr;
            while (*ptr && *ptr != '>') ptr++;
            
            char tagName[100];
            size_t len = ptr - tagNameStart;
            if(len>99) len=99;
            strncpy(tagName, tagNameStart, len);
            tagName[len] = 0;
            toLowerStr(tagName);

            HtmlNode* topNode = peek(stack);
            if (topNode && topNode->tagName && strcmp(tagName, topNode->tagName) == 0) {
                pop(stack)->endPos = (ptr - G_htmlContent) + 1;
            } else {
                // 栈顶不匹配，这里采取往下看看，如果匹配就认为是中间多余起始标签或缺少结束标签
                int find = -1;
                for(int i = stack->top; i>=0 && i>= stack->top-3; i--){
                    // 这里限制最多往下搜索3次，以防崩溃
                    if(strcmp(tagName, stack->items[i]->tagName)==0){
                        find = i;
                        break;
                    }
                }
                if(find!=-1){
                    while(stack->top >= find){
                        HtmlNode* node = pop(stack);
                        // 给这些被迫闭合的节点一个结束位置,防止包含的内容被忽略
                        if (node->endPos == -1) node->endPos = ptr - G_htmlContent;
                        // 如果是目标节点，正常设置位置
                        if(strcmp(node->tagName,tagName)==0){
                            node->endPos = (ptr - G_htmlContent) + 1;
                        }
                    }
                    printf("Recovered from mismatched tag: closed up to %s\n", tagName);// 打印修复信息
                }else {
                // 还是没找到（比如多余的 </div>），那只能忽略这个闭合标签了
                printf("Warning: Ignoring stray closing tag </%s>\n", tagName);
                }
            }
            if (*ptr == '>') ptr++;
        } 
        // Opening Tag
        else {
            char* tagNameStart = ptr;
            while (*ptr && !isspace((unsigned char)*ptr) && *ptr != '>' && *ptr != '/') ptr++;
            
            char tagName[100];
            size_t len = ptr - tagNameStart;
            if(len>99) len=99;
            strncpy(tagName, tagNameStart, len);
            tagName[len] = 0;
            toLowerStr(tagName);

            HtmlNode* newNode = createNode(ELEMENT_NODE);
            newNode->tagName = strdup(tagName);
            newNode->startPos = tagStartPos;

            // --- Attribute Parsing ---
            while (*ptr && *ptr != '>' && *ptr != '/') {
                if (isspace((unsigned char)*ptr)) { ptr++; continue; }
                
                char* keyStart = ptr;
                while (*ptr && !isspace((unsigned char)*ptr) && *ptr != '=' && *ptr != '>' && *ptr != '/') ptr++;
                char key[100] = {0};
                size_t kLen = ptr - keyStart;
                if(kLen > 99) kLen = 99;
                strncpy(key, keyStart, kLen);
                
                char* value = NULL;
                if (*ptr == '=') {
                    ptr++;
                    while(isspace((unsigned char)*ptr)) ptr++;
                    
                    char quote = 0;
                    if (*ptr == '"' || *ptr == '\'') {
                        quote = *ptr;
                        ptr++;
                        char* valStart = ptr;
                        while (*ptr && *ptr != quote) ptr++;
                        value = strndup_custom(valStart, ptr - valStart);
                        if (*ptr == quote) ptr++;
                    } else {
                        char* valStart = ptr;
                        while (*ptr && !isspace((unsigned char)*ptr) && *ptr != '>' && *ptr != '/') ptr++;
                        value = strndup_custom(valStart, ptr - valStart);
                    }
                }
                addAttribute(newNode, key, value ? value : "");
                if(value) free(value);
            }
            // -------------------------

            // Check self closing / >
            int isSelfClosing = 0;
            if (*ptr == '/') {
                isSelfClosing = 1;
                ptr++;
            }
            if (*ptr == '>') ptr++;
            
            newNode->endPos = ptr - G_htmlContent;
            addChild(peek(stack), newNode);

            // Special tags
            if (strcmp(tagName, "script") == 0 || strcmp(tagName, "style") == 0) {
                char endTag[110];
                sprintf(endTag, "</%s>", tagName);
                char* endPtr = strstr(ptr, endTag);
                if (endPtr) {
                    ptr = endPtr + strlen(endTag);
                    newNode->endPos = ptr - G_htmlContent;
                }
                continue; // Don't push to stack
            }

            if (!isSelfClosing && !isVoidElement(tagName)) {
                push(stack, newNode);
            }
        }
    }
    freeStack(stack);
}

// --- CSS Selector Engine ---

// Check if a single node matches a "Simple Selector" (e.g., "div#myid.class1.class2")
// --- CSS Selector Engine (Modified) ---

// 辅助函数：判断 str 是否以 prefix 开头
int strStartsWith(const char* str, const char* prefix) {
    if (!str || !prefix) return 0;
    size_t len_pre = strlen(prefix);
    size_t len_str = strlen(str);
    return len_str < len_pre ? 0 : strncmp(prefix, str, len_pre) == 0;
}

// 核心匹配函数
int matchNode(HtmlNode* node, const char* simpleSelector) {
    if (node->type != ELEMENT_NODE) return 0;
    if (strcmp(simpleSelector, "*") == 0) return 1;

    char* sel = strdup(simpleSelector);
    char* cur = sel;
    int match = 1;
    
    // 1. Tag Name (标签名匹配)
    char* tokenEnd = cur;
    // 修改点：增加了 '[' 作为分隔符检查
    while (*tokenEnd && *tokenEnd != '#' && *tokenEnd != '.' && *tokenEnd != '[') tokenEnd++;
    
    if (tokenEnd > cur) {
        char tag[100];
        size_t len = tokenEnd - cur;
        strncpy(tag, cur, len);
        tag[len] = 0;
        toLowerStr(tag);
        if (strcmp(tag, node->tagName) != 0 && strcmp(tag, "*") != 0) match = 0;
        cur = tokenEnd;
    }

    // 2. ID, Classes, and Attributes (ID, Class, 属性选择器)
    while (*cur && match) {
        char type = *cur; // '#', '.', or '['
        
        if (type == '[') {
            // --- 处理属性选择器 (修复版) ---
            cur++; // 跳过 '['
            char* endBracket = strchr(cur, ']');
            if (!endBracket) { match = 0; break; } // 语法错误：没有闭合
            
            // 提取括号内容
            size_t contentLen = endBracket - cur;
            char* attrExpr = (char*)malloc(contentLen + 1);
            strncpy(attrExpr, cur, contentLen);
            attrExpr[contentLen] = 0;
            
            // 查找操作符 (=, ~=, |=, ^=)
            char* key = NULL;
            char* val = NULL;
            char op[3] = {0}; 
            
            char* eqPos = strchr(attrExpr, '=');
            
            if (!eqPos) {
                // [attribute] 仅检查是否存在
                key = strdup(attrExpr);
                // 修复 1: 属性名忽略大小写并去空格
                char* tempKey = trimWhitespace(key);
                free(key); key = tempKey;
                toLowerStr(key); 

                char* nodeVal = getAttribute(node, key);
                if (!nodeVal) match = 0;
            } else {
                // 解析操作符
                char prev = (eqPos > attrExpr) ? *(eqPos - 1) : 0;
                
                if (prev == '~' || prev == '|' || prev == '^') {
                    op[0] = prev; op[1] = '='; op[2] = 0;
                    size_t keyLen = (eqPos - 1) - attrExpr;
                    key = strndup_custom(attrExpr, keyLen);
                } else {
                    op[0] = '='; op[1] = 0;
                    size_t keyLen = eqPos - attrExpr;
                    key = strndup_custom(attrExpr, keyLen);
                }

                // 修复 2: 规范化 Key (去空格 + 转小写)
                char* cleanKey = trimWhitespace(key);
                free(key); key = cleanKey;
                toLowerStr(key); 
                
                // 提取 value (智能处理引号和空格)
                char* rawVal = eqPos + 1;
                // 跳过等号后的空格 (例如 [att = val])
                while(*rawVal && isspace((unsigned char)*rawVal)) rawVal++;

                if (*rawVal == '"' || *rawVal == '\'') {
                    char quote = *rawVal;
                    rawVal++;
                    char* quoteEnd = strrchr(rawVal, quote);
                    if (quoteEnd) *quoteEnd = 0; 
                    val = strdup(rawVal); // 引号内的内容保留原样
                } else {
                    val = strdup(rawVal);
                    // 修复 3: 如果没引号，必须去掉末尾空格
                    char* cleanVal = trimWhitespace(val);
                    free(val); val = cleanVal;
                }
                
                // 获取节点实际属性值
                char* nodeVal = getAttribute(node, key);
                
                if (!nodeVal) {
                    match = 0;
                } else {
                    // --- 核心匹配逻辑 ---
                    
                    // [attribute=value] 完全匹配
                    if (strcmp(op, "=") == 0) {
                        if (strcmp(nodeVal, val) != 0) match = 0;
                    }
                    // [attribute~=value] 包含单词 (修复: 增强分隔符)
                    else if (strcmp(op, "~=") == 0) {
                        // 如果 val 为空或包含空格，则必定不匹配
                        if (val[0] == '\0' || strpbrk(val, " \t\n\r\f")) {
                            match = 0;
                        } else {
                            char* tempNv = strdup(nodeVal);
                            // 修复 4: 使用所有标准空白符分割 (\t, \n 等)
                            char* p = strtok(tempNv, " \t\n\r\f");
                            int found = 0;
                            while(p) {
                                if(strcmp(p, val) == 0) { found = 1; break; }
                                p = strtok(NULL, " \t\n\r\f");
                            }
                            free(tempNv);
                            if (!found) match = 0;
                        }
                    }
                    // [attribute|=value] 开头匹配
                    else if (strcmp(op, "|=") == 0) {
                        size_t vLen = strlen(val);
                        if (vLen == 0) {
                            match = 0;
                        } else {
                            if (strcmp(nodeVal, val) == 0) {
                                // 匹配
                            } else if (strlen(nodeVal) > vLen && 
                                     strncmp(nodeVal, val, vLen) == 0 && 
                                     nodeVal[vLen] == '-') {
                                // 匹配 "val-" 开头
                            } else {
                                match = 0;
                            }
                        }
                    }
                    // [attribute^=value] 前缀匹配
                    else if (strcmp(op, "^=") == 0) {
                        if (strlen(val) == 0 || !strStartsWith(nodeVal, val)) match = 0;
                    }
                }
            }
            
            if(key) free(key);
            if(val) free(val);
            free(attrExpr);
            
            cur = endBracket + 1; 
            continue;
        }
        
        // --- 下面是原有的 ID 和 Class 处理逻辑 (稍微调整了 tokenEnd 查找) ---
        cur++; // skip '#' or '.'
        tokenEnd = cur;
        // 修改点：增加了 '['
        while (*tokenEnd && *tokenEnd != '#' && *tokenEnd != '.' && *tokenEnd != '[') tokenEnd++;
        
        char val[100];
        size_t len = tokenEnd - cur;
        strncpy(val, cur, len);
        val[len] = 0;

        if (type == '#') {
            char* id = getAttribute(node, "id");
            if (!id || strcmp(id, val) != 0) match = 0;
        } else if (type == '.') {
            char* cls = getAttribute(node, "class");
            if (!cls) {
                match = 0;
            } else {
                char* clsCopy = strdup(cls);
                char* p = strtok(clsCopy, " ");
                int found = 0;
                while (p) {
                    if (strcmp(p, val) == 0) { found = 1; break; }
                    p = strtok(NULL, " ");
                }
                free(clsCopy);
                if (!found) match = 0;
            }
        }
        cur = tokenEnd;
    }
    free(sel);
    return match;
}

// Add a node to the list if unique
void addToResult(NodeList* list, HtmlNode* node) {
    for (int i = 0; i < list->count; i++) {
        if (list->nodes[i] == node) return;
    }
    list->nodes = (HtmlNode**)realloc(list->nodes, sizeof(HtmlNode*) * (list->count + 1));
    list->nodes[list->count++] = node;
}

// Helper: Split selector by comma and process groups
NodeList* querySelectorAll(HtmlNode* root, const char* selector) {
    NodeList* result = (NodeList*)calloc(1, sizeof(NodeList));
    char* selCopy = strdup(selector);
    char* group = strtok(selCopy, ",");
    
    while (group) {
        char* cleanGroup = trimWhitespace(group);
        if (strlen(cleanGroup) > 0) {
            // We need a temporary list for this group
            NodeList* groupRes = (NodeList*)calloc(1, sizeof(NodeList));
            
            // Logic to parse relationships: A > B, A + B, A B
            // Simplified: We assume strict L to R processing.
            // 1. Tokenize by space/combinators
            
            // Initial set: Recursively find matching first part
            // But complex selectors are hard.
            // Let's implement a recursive matcher that takes a set of candidates and filters them.
            
            // Step 1: Get all nodes from root (flattened)
            // Step 2: Apply logic.
            // Optimized approach for this assignment:
            // "A B" -> Find A, then inside A find B.
            
            // We will parse the selector into parts.
            // E.g. "div.cls > p" -> ["div.cls", ">", "p"]
            
            // For simplicity in C without a full lexer:
            // We will do a full recursive search for the *Rightmost* part, 
            // then verify parents/siblings up the chain.
            // This is how browser engines often optimize (Right-to-Left matching).
            
            // For this assignment, let's stick to Left-to-Right recursive search
            // because it's easier to debug for "Experiment 03".
            
            // Find the FIRST selector part
            // Parse logic: scan string, stop at space, >, +, ~
            // Note: This needs a custom tokenizer loop.
            
            // Shortcut: call a recursive solver
            queryRecursive(root, cleanGroup, result);
        }
        group = strtok(NULL, ",");
    }
    free(selCopy);
    return result;
}


void filterNodes(NodeList* source, char combinator, char* selector, NodeList* dest) {
    char* sel = trimWhitespace(selector);
    for (int i = 0; i < source->count; i++) {
        HtmlNode* ctx = source->nodes[i];
        
        if (combinator == ' ') { // Descendant: any node below ctx
             Stack* s = createStack(50);
             if(ctx->type == ELEMENT_NODE) {
                 for(int k=0; k<ctx->childCount; k++) push(s, ctx->children[k]);
             }
             while(s->top > -1){
                 HtmlNode* n = pop(s);
                 if(matchNode(n, sel)) addToResult(dest, n);
                 if(n->type == ELEMENT_NODE) {
                    for(int k=n->childCount-1; k>=0; k--) push(s, n->children[k]);
                 }
             }
             freeStack(s);
        }
        else if (combinator == '>') { // Child: immediate children
            for (int k = 0; k < ctx->childCount; k++) {
                if (matchNode(ctx->children[k], sel)) addToResult(dest, ctx->children[k]);
            }
        }
        else if (combinator == '+') { // Adjacent Sibling
            if (ctx->parent) {
                int foundSelf = 0;
                for (int k = 0; k < ctx->parent->childCount; k++) {
                    if (ctx->parent->children[k] == ctx) {
                        foundSelf = 1;
                        continue; // 找到自己，跳到下一个节点
                    }
                    
                    if (foundSelf) {
                        HtmlNode* next = ctx->parent->children[k];

                        // --- 核心修复：跳过空白文本节点 ---
                        if (next->type == TEXT_NODE) {
                            // 仅跳过空白文本节点，继续检查下一个兄弟
                            if (isWhitespace(next->textContent)) continue; 
                            // 如果文本节点非空白（例如包含可见字符），则它仍然是干扰项，我们必须停止。
                        }
                        
                        // 找到第一个非TEXT_NODE 或非空白的兄弟节点
                        if (matchNode(next, sel)) {
                            addToResult(dest, next);
                        }
                        break; // 无论是匹配成功还是遇到非目标元素，都只找紧邻的第一个，然后停止。
                    }
                }
            }
        }
        else if (combinator == '~') { // General Sibling
            if (ctx->parent) {
                int foundSelf = 0;
                for (int k = 0; k < ctx->parent->childCount; k++) {
                    if (ctx->parent->children[k] == ctx) foundSelf = 1;
                    else if (foundSelf) {
                        HtmlNode* sibling = ctx->parent->children[k];
                        if (sibling->type == TEXT_NODE && isWhitespace(sibling->textContent)) {
                            continue; // 跳过中间的空白文本节点
                        }
                        
                        if (matchNode(sibling, sel)) {
                            addToResult(dest, sibling);
                        }
                        // 如果这里不跳过非匹配元素，则可以继续
                    }
                }
            }
        }
        else if (combinator == 0) { // Root/Global context (handled specially)
             // Should not happen in filter step usually
        }
    }
    free(sel);
}

void queryRecursive(HtmlNode* root, const char* selector, NodeList* results) {
    char* sel = strdup(selector);
    
    // 初始集合：仅包含根节点
    NodeList currentSet = {0};
    currentSet.nodes = (HtmlNode**)malloc(sizeof(HtmlNode*));
    currentSet.nodes[0] = root;
    currentSet.count = 1;

    char* ptr = sel;
    char currentComb = ' '; // 默认第一层关系是后代（或者理解为从根开始搜）
    
    while(*ptr) {
        // --- 核心修复开始：智能分词 ---
        char token[256] = {0}; // 扩大缓冲区防止溢出
        char nextComb = 0;
        char* start = ptr;
        
        int inBracket = 0; // 括号计数器
        
        // 扫描直到遇到“不在括号内”的组合器
        while (*ptr) {
            // 1. 维护括号状态
            if (*ptr == '[') inBracket++;
            else if (*ptr == ']') inBracket--;

            // 2. 只有在括号外 (inBracket == 0) 才允许识别分隔符
            if (inBracket == 0) {
                if (*ptr == '>' || *ptr == '+' || *ptr == '~') {
                    nextComb = *ptr;
                    break;
                }
                
                // 检查空格是否作为组合器
                if (isspace((unsigned char)*ptr)) {
                    // 向前看：如果空格后面紧跟其他符号(>+~)，则该空格只是格式化，不是后代组合器
                    char* temp = ptr + 1; 
                    while(*temp && isspace((unsigned char)*temp)) temp++;
                    
                    if (*temp == '>' || *temp == '+' || *temp == '~') {
                        // 让外层循环处理那个符号
                        ptr = temp; 
                        continue; 
                    } else if (*temp) {
                        // 空格后面是文字，说明这是一个后代组合器 (Descendant Combinator)
                        nextComb = ' ';
                        break;
                    }
                }
            }
            ptr++;
        }
        // --- 核心修复结束 ---
        
        // 复制 Token (例如 "div[title~=flower]")
        int len = ptr - start;
        if(len > 255) len = 255;
        strncpy(token, start, len);
        token[len] = 0; // 确保结尾
        
        char* cleanToken = trimWhitespace(token);
        
        // 执行过滤
        NodeList nextSet = {0};
        if (strlen(cleanToken) > 0) {
            filterNodes(&currentSet, currentComb, cleanToken, &nextSet);
            
            // 释放旧集合，切换到新集合
            free(currentSet.nodes);
            currentSet = nextSet;
        } 
        free(cleanToken);

        if (*ptr == 0) break; // 字符串结束

        // 为下一次循环准备 Combinator
        if (nextComb != ' ') {
            currentComb = nextComb;
            ptr++; // 跳过符号 (> + ~)
            while (*ptr && isspace((unsigned char)*ptr)) ptr++; // 跳过符号后的空格
        } else {
            currentComb = ' ';
            // 跳过刚才作为组合器的空格
            while(*ptr && isspace((unsigned char)*ptr)) ptr++;
        }
    }

    // 将最终结果加入 results
    for(int i=0; i<currentSet.count; i++) {
        addToResult(results, currentSet.nodes[i]);
    }
    if(currentSet.nodes) free(currentSet.nodes);
    free(sel);
}
// --- Output Functions ---

void printNodeList(NodeList* list) {
    if (!list || list->count == 0) {
        printf("Result: [] (0 items)\n");
        return;
    }
    printf("Result: [");
    for (int i = 0; i < list->count; i++) {
        HtmlNode* node = list->nodes[i];
        printf("%s", node->tagName);
        char* id = getAttribute(node, "id");
        if(id) printf("#%s", id);
        char* cls = getAttribute(node, "class");
        if(cls) {
            // Simplify class output for display
            char* c = strdup(cls);
            char* p = strtok(c, " ");
            while(p) { printf(".%s", p); p = strtok(NULL, " "); }
            free(c);
        }
        if (i < list->count - 1) printf(", ");
    }
    printf("] (%d items)\n", list->count);
}

// --- 辅助函数：定义在外部 ---
// 注意：buf 需要传二级指针 (char**)，因为 realloc 可能会改变内存地址
// len 需要传指针 (size_t*)，以便累加长度
void extractTextRecursiveHelper(HtmlNode* n, char** buf, size_t* len) {
    if (!n) return;

    if (n->type == TEXT_NODE && n->textContent) {
        size_t l = strlen(n->textContent);
        // 重新分配内存：当前长度 + 新文本长度 + 空格/终结符
        char* new_ptr = (char*)realloc(*buf, *len + l + 2);
        if (!new_ptr) return; // 简单的错误检查
        *buf = new_ptr;

        strcpy(*buf + *len, n->textContent);
        *len += l;
        (*buf)[*len] = ' '; // 添加空格分隔
        (*buf)[*len + 1] = 0;
        (*len)++;
    } 
    else if (n->type == ELEMENT_NODE) {
        for (int i = 0; i < n->childCount; i++) {
            extractTextRecursiveHelper(n->children[i], buf, len);
        }
        // 如果是块级元素，添加换行
        if (isBlockTag(n->tagName)) {
            char* new_ptr = (char*)realloc(*buf, *len + 2);
            if (new_ptr) {
                *buf = new_ptr;
                (*buf)[*len] = '\n';
                (*buf)[*len + 1] = 0;
                (*len)++;
            }
        }
    }
}

// --- 主函数 ---
char* getInnerText(HtmlNode* node) {
    if (!node) return strdup("");
    
    // 初始化 buffer
    char* buf = (char*)calloc(1, 1);
    size_t len = 0;
    
    // 调用辅助函数，传入 buf 和 len 的地址
    if (node->type == ELEMENT_NODE) {
       for (int i = 0; i < node->childCount; i++) {
           extractTextRecursiveHelper(node->children[i], &buf, &len);
       }
    } else {
        extractTextRecursiveHelper(node, &buf, &len);
    }
    
    return buf;
}

char* getOuterHTML(HtmlNode* node) {
    if(!node || node->startPos == -1 || node->endPos == -1) return strdup("");
    int len = node->endPos - node->startPos;
    if (len <= 0) return strdup("");
    char* buf = malloc(len + 1);
    strncpy(buf, G_htmlContent + node->startPos, len);
    buf[len] = 0;
    return buf;
}

// --- Utils ---

char* trimWhitespace(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return strdup("");
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    char* copy = malloc(end - str + 2);
    strncpy(copy, str, end - str + 1);
    copy[end - str + 1] = 0;
    return copy;
}

// ... (Rest of original utility functions: isVoidElement, isBlockTag, createNode, createStack etc. remain similar) ...
// Included strictly necessary ones here for compilation

int isVoidElement(const char* tag) {
    const char* voidTags[] = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr", NULL};
    for (int i = 0; voidTags[i]; ++i) if (strcmp(tag, voidTags[i]) == 0) return 1;
    return 0;
}

int isBlockTag(const char* tag) {
    const char* blockTags[] = {"p", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "div", "table", "tr", "form", "body", "html", NULL};
    for (int i = 0; blockTags[i]; ++i) if (strcmp(tag, blockTags[i]) == 0) return 1;
    return 0;
}

int isWhitespace(const char* str) {
    while (*str) { if (!isspace((unsigned char)*str)) return 0; str++; }
    return 1;
}

void toLowerStr(char* str) {
    for (; *str; ++str) *str = tolower((unsigned char)*str);
}

char* strndup_custom(const char* s, size_t n) {
    char* p = (char*)malloc(n + 1);
    if (p) { strncpy(p, s, n); p[n] = 0; }
    return p;
}

HtmlNode* createNode(NodeType type) {
    HtmlNode* node = (HtmlNode*)calloc(1, sizeof(HtmlNode));
    node->type = type;
    node->startPos = -1; node->endPos = -1;
    return node;
}

void addChild(HtmlNode* parent, HtmlNode* child) {
    if (parent->childCount >= parent->capacity) {
        parent->capacity = (parent->capacity == 0) ? 8 : parent->capacity * 2;
        parent->children = (HtmlNode**)realloc(parent->children, sizeof(HtmlNode*) * parent->capacity);
    }
    parent->children[parent->childCount++] = child;
    child->parent = parent;
}

void freeTree(HtmlNode* node) {
    if (!node) return;
    for (int i = 0; i < node->childCount; ++i) freeTree(node->children[i]);
    free(node->tagName);
    free(node->textContent);
    Attribute* attr = node->attributes;
    while(attr) {
        Attribute* next = attr->next;
        free(attr->key); free(attr->value); free(attr);
        attr = next;
    }
    free(node->children);
    free(node);
}

// Stack impl
Stack* createStack(int capacity) {
    Stack* s = (Stack*)malloc(sizeof(Stack));
    s->items = (HtmlNode**)malloc(sizeof(HtmlNode*) * capacity);
    s->top = -1; s->capacity = capacity;
    return s;
}
void push(Stack* s, HtmlNode* node) { if (s->top < s->capacity - 1) s->items[++s->top] = node; }
HtmlNode* pop(Stack* s) { return (s->top > -1) ? s->items[s->top--] : NULL; }
HtmlNode* peek(Stack* s) { return (s->top > -1) ? s->items[s->top] : NULL; }
void freeStack(Stack* s) { free(s->items); free(s); }

// Network
char* fetchHtmlFromUrl(const char* url) {
    CURL* curl;
    CURLcode res;
    MemoryStruct chunk;
    chunk.buffer = (char*)malloc(1); chunk.size = 0;
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); 
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) { free(chunk.buffer); chunk.buffer = NULL; }
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    return chunk.buffer;
}
static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    MemoryStruct* mem = (MemoryStruct*)userp;
    char* ptr = (char*)realloc(mem->buffer, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->buffer = ptr;
    memcpy(&(mem->buffer[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->buffer[mem->size] = 0;
    return realsize;
}
char* readHtmlFromFile(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* content = (char*)malloc(fileSize + 1);
    if (content) { fread(content, 1, fileSize, file); content[fileSize] = 0; }
    fclose(file);
    return content;
}