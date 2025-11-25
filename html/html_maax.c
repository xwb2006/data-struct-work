#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

// --- Definitions & Structs ---

typedef enum { ELEMENT_NODE, TEXT_NODE } NodeType;

// Attribute structure for id, class, href, etc.
typedef struct Attribute {
    char* key;
    char* value;
    struct Attribute* next;
} Attribute;

typedef struct HtmlNode {
    NodeType type;
    char* tagName;
    char* textContent;
    Attribute* attributes; // Linked list of attributes
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

typedef struct {
    char* buffer;
    size_t size;
} MemoryStruct;

// --- Global Variables ---
char* G_htmlContent = NULL;
HtmlNode* G_rootNode = NULL;
NodeList* G_lastResult = NULL; // Stores the result of the last query for "Out[k]" access
int G_parsingError = 0;

// --- Function Declarations ---

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

// --- Main ---

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
    if(G_parsingError) printf("Warning: HTML parsing encountered errors, DOM may be incomplete.\n");

    printf("\nHTML Loaded. Enter commands.\n");
    printf("Examples:\n");
    printf("  query div.content     (CSS Selector search)\n");
    printf("  Out[0].innerText      (Get text of 1st result)\n");
    printf("  Out[0].outerHTML      (Get HTML of 1st result)\n");
    printf("  Out[0].href           (Get href if <a> tag)\n");
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
            char prop[50];
            // Parse: Out[k].property
            char* ptr = command + 4;
            if (sscanf(ptr, "%d].%s", &index, prop) == 2) {
                if (!G_lastResult || index < 0 || index >= G_lastResult->count) {
                    printf("Error: Index %d out of bounds (Count: %d)\n", index, G_lastResult ? G_lastResult->count : 0);
                } else {
                    HtmlNode* node = G_lastResult->nodes[index];
                    if (strcmp(prop, "innerText") == 0) {
                        char* text = getInnerText(node);
                        printf("%s\n", text);
                        free(text);
                    } else if (strcmp(prop, "outerHTML") == 0) {
                        char* html = getOuterHTML(node);
                        printf("%s\n", html);
                        free(html);
                    } else if (strcmp(prop, "href") == 0) {
                        char* href = getAttribute(node, "href");
                        printf("%s\n", href ? href : "(null)");
                    } else if (strcmp(prop, "tagName") == 0) {
                        printf("%s\n", node->tagName);
                    } else {
                        printf("Unknown property: %s\n", prop);
                    }
                }
            } else {
                printf("Invalid format. Use Out[k].property\n");
            }
        } else {
            printf("Unknown command.\n");
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
                // Simplified mismatch handling: just ignore or close implictly
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
int matchNode(HtmlNode* node, const char* simpleSelector) {
    if (node->type != ELEMENT_NODE) return 0;
    if (strcmp(simpleSelector, "*") == 0) return 1;

    char* sel = strdup(simpleSelector);
    char* cur = sel;
    int match = 1;
    
    // 1. Tag Name
    char* tokenEnd = cur;
    while (*tokenEnd && *tokenEnd != '#' && *tokenEnd != '.') tokenEnd++;
    
    if (tokenEnd > cur) {
        char tag[100];
        size_t len = tokenEnd - cur;
        strncpy(tag, cur, len);
        tag[len] = 0;
        toLowerStr(tag);
        if (strcmp(tag, node->tagName) != 0 && strcmp(tag, "*") != 0) match = 0;
        cur = tokenEnd;
    }

    // 2. ID and Classes
    while (*cur && match) {
        char type = *cur; // '#' or '.'
        cur++;
        tokenEnd = cur;
        while (*tokenEnd && *tokenEnd != '#' && *tokenEnd != '.') tokenEnd++;
        
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
                // Check if val is in cls (space delimited)
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

// This function processes a specific selector chain (e.g. "div > p")
// It's a simplified recursive descent.
void matchRecursive(HtmlNode* currentScope, char* selectorStr, NodeList* finalResult) {
    // 1. Parse the immediate selector (head)
    char* s = trimWhitespace(selectorStr);
    if (!*s) return;

    char head[100] = {0};
    char combinator = 0; // 0 (descendant), '>', '+', '~'
    char* rest = NULL;

    // Scan for separator
    char* ptr = s;
    while (*ptr) {
        if (*ptr == '>' || *ptr == '+' || *ptr == '~') {
            combinator = *ptr;
            strncpy(head, s, ptr - s);
            rest = ptr + 1; // Skip combinator
            break;
        }
        if (isspace((unsigned char)*ptr)) {
            // Might be descendant combinator, OR just space before >,+,~
            // Check next non-space char
            char* next = ptr + 1; 
            while (*next && isspace((unsigned char)*next)) next++;
            if (*next == '>' || *next == '+' || *next == '~') {
                combinator = *next;
                strncpy(head, s, ptr - s);
                rest = next + 1;
                ptr = next; // Update ptr to break loop
                break;
            } else if (*next) {
                combinator = ' '; // Descendant
                strncpy(head, s, ptr - s);
                rest = next;
                break;
            }
        }
        ptr++;
    }
    if (combinator == 0) {
        strcpy(head, s); // Last part
    }
    
    // Trim head
    char* cleanHead = trimWhitespace(head);

    // Find all nodes in currentScope that match cleanHead
    // If combinator is ' ', find all descendants
    // If combinator is '>', find children
    // If combinator is '+', adjacent sibling
    // If combinator is '~', general sibling
    // BUT: The standard logic is: Find X matching Head. Then for each X, find Y matching Rest.
    
    // Collection of nodes matching 'head' relative to currentScope
    NodeList candidates = {0, 0};
    
    if (currentScope == G_rootNode && strcmp(currentScope->tagName, "document_root") == 0) {
        // Initial entry, search everywhere for Head
         // Assuming starting combinator is always effectively "descendant" from root
         // except root isn't an element.
         for(int i=0; i<currentScope->childCount; i++) {
             // Helper to traverse tree and match
             // We need a helper to collect all nodes matching 'cleanHead' under 'currentScope'
             // Since 'currentScope' is document root, we behave like Descendant
            Stack* s = createStack(100);
            push(s, currentScope->children[i]);
            while(s->top > -1){
                HtmlNode* n = pop(s);
                if(matchNode(n, cleanHead)) addToResult(&candidates, n);
                for(int k=n->childCount-1; k>=0; k--) push(s, n->children[k]);
            }
            freeStack(s);
         }
    } else {
        // We are inside a chain. Context matters.
        // Wait, the recursion should pass the CONTEXT type.
        // To simplify: I will only implement "Find Matches" logic here.
    }
}

// --- Simplified "Left-to-Right" Selector Implementation --- 
// Because writing a full CSS parser in one file is massive, 
// we implement the logic: Split by combinator, filter list.

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
                for (int k = 0; k < ctx->parent->childCount - 1; k++) {
                    if (ctx->parent->children[k] == ctx) {
                        HtmlNode* next = ctx->parent->children[k+1];
                        if (matchNode(next, sel)) addToResult(dest, next);
                        break;
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
                        if (matchNode(ctx->parent->children[k], sel)) addToResult(dest, ctx->parent->children[k]);
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
    // 1. Tokenize Selector Chain: "div > p .cls" -> [("div", ' '), ("p", '>'), (".cls", ' ')]
    // We cheat: We process one step, generate a list, pass list to next step.
    
    char* sel = strdup(selector);
    
    // Initial List: Contains just the Root
    NodeList currentSet = {0};
    // Special case: Root is a dummy document_root, so "descendant" of root is actually "child" of root logic roughly
    // But let's seed with ROOT, and treat first combinator as ' ' (descendant)
    currentSet.nodes = malloc(sizeof(HtmlNode*));
    currentSet.nodes[0] = root;
    currentSet.count = 1;

    char* ptr = sel;
    char currentComb = ' '; 
    
    while(*ptr) {
        // Extract next simple selector part
        char token[100] = {0};
        char nextComb = 0;
        char* start = ptr;
        
        // Advance until combinator
        while (*ptr) {
            if (*ptr == '>' || *ptr == '+' || *ptr == '~') {
                nextComb = *ptr;
                break;
            }
            // Check for space acting as combinator
            if (isspace((unsigned char)*ptr)) {
                // Peek ahead
                char* temp = ptr + 1; 
                while(*temp && isspace((unsigned char)*temp)) temp++;
                if (*temp == '>' || *temp == '+' || *temp == '~') {
                    // The space is just formatting for another combinator
                    ptr = temp; // Let the loop catch the symbol next iteration
                    continue; 
                } else if (*temp) {
                    // Implicit descendant combinator
                    nextComb = ' ';
                    break;
                }
            }
            ptr++;
        }
        
        // Copy token
        int len = ptr - start;
        if(len > 99) len = 99;
        strncpy(token, start, len);
        char* cleanToken = trimWhitespace(token);
        
        // Perform filtering
        NodeList nextSet = {0};
        if (strlen(cleanToken) > 0) {
            filterNodes(&currentSet, currentComb, cleanToken, &nextSet);
            // Swap sets
            free(currentSet.nodes);
            currentSet = nextSet;
        } else {
             // Empty token (e.g. multiple spaces), ignore
        }
        free(cleanToken);

        if (*ptr == 0) break; // End of string

        // Setup for next loop
        if (nextComb != ' ') {
            currentComb = nextComb;
            ptr++; // Skip the symbol
        } else {
            currentComb = ' ';
            // Skip the space we found
            while(isspace((unsigned char)*ptr)) ptr++;
        }
    }

    // Add final currentSet to results
    for(int i=0; i<currentSet.count; i++) {
        addToResult(results, currentSet.nodes[i]);
    }
    free(currentSet.nodes);
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

char* getInnerText(HtmlNode* node) {
    if (!node) return strdup("");
    
    // Calculate size first
    size_t size = 1;
    Stack* s = createStack(50);
    push(s, node);
    // ... Actually recursion is easier for text extraction
    // Let's use a simpler recursive approach with a growing buffer
    
    // Temporary: static buffer for simplicity (Assignment says "OutHTML/Text", can be large)
    // Dynamic approach:
    char* buf = calloc(1, 1);
    size_t len = 0;
    
    // Internal recursive helper
    void extract(HtmlNode* n) {
        if(n->type == TEXT_NODE && n->textContent) {
            size_t l = strlen(n->textContent);
            buf = realloc(buf, len + l + 2);
            strcpy(buf + len, n->textContent);
            len += l;
            buf[len] = ' '; // add space separator
            buf[len+1] = 0;
            len++;
        } else if (n->type == ELEMENT_NODE) {
            for(int i=0; i<n->childCount; i++) extract(n->children[i]);
            if(isBlockTag(n->tagName)) {
                buf = realloc(buf, len + 2);
                buf[len] = '\n';
                buf[len+1] = 0;
                len++;
            }
        }
    }
    
    if(node->type == ELEMENT_NODE) {
       for(int i=0; i<node->childCount; i++) extract(node->children[i]);
    } else {
        extract(node);
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