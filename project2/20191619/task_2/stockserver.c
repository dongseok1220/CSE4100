/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"
#define NTHREADS 20
#define SBUFSIZE 1024

/* sbuf Package */
typedef struct {
    int *buf; /* Buffer array */
    int n; /* Maximum number of slots */
    int front; /* buf[(front+1)%n] is first item */
    int rear; /* buf[rear%n] is last item */
    sem_t mutex; /* Protects accesses to buf */
    sem_t slots; /* Counts available slots */
    sem_t items; /* Counts available items */
} sbuf_t;

sbuf_t sbuf; /* Shared buffer of connected descriptors */

void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n; /* Buffer holds max of n items */
    sp->front = sp->rear = 0; /* Empty buffer iff front == rear */
    Sem_init(&sp->mutex, 0, 1); /* Binary semaphore for locking */
    Sem_init(&sp->slots, 0, n); /* Initially, buf has n empty slots */
    Sem_init(&sp->items, 0, 0); /* Initially, buf has 0 items */
}
/* Clean up buffer sp */
void sbuf_deinit(sbuf_t *sp)
{
    Free(sp->buf);
}

/* Insert item onto the rear of shared buffer sp */
void sbuf_insert(sbuf_t *sp, int item)
{
    P(&sp->slots); /* Wait for available slot */
    P(&sp->mutex); /* Lock the buffer */
    sp->buf[(++sp->rear)%(sp->n)] = item; /* Insert the item */
    V(&sp->mutex); /* Unlock the buffer */
    V(&sp->items); /* Announce available item */
}

/* Remove and return the first item from buffer sp */
int sbuf_remove(sbuf_t *sp)
{
    int item;
    P(&sp->items); /* Wait for available item */
    P(&sp->mutex); /* Lock the buffer */
    item = sp->buf[(++sp->front)%(sp->n)]; /* Remove the item */
    V(&sp->mutex); /* Unlock the buffer */
    V(&sp->slots); /* Announce available slot */
    return item;
}

/* 바이너리 트리 구현 부분 */
typedef struct item {
    int ID;
    int left_stock;
    int price;
    int readcnt;
    sem_t mutex; 
    sem_t w;
}item; 

typedef struct tree_node {
    item data; 
    struct tree_node* l_node, *r_node; 
}tree_node; 

tree_node* root = NULL;

// 삽입
tree_node* insert_tree(tree_node* node, item data) {
    if (node == NULL) {
        node = (tree_node*)malloc(sizeof(tree_node));
        node->data = data;
        node->l_node = NULL;
        node->r_node = NULL;
        Sem_init(&(node->data.mutex), 0, 1);
		Sem_init(&(node->data.w), 0, 1);
    } else if (data.ID < node->data.ID) {
        node->l_node = insert_tree(node->l_node, data);
    } else if (data.ID > node->data.ID) {
        node->r_node = insert_tree(node->r_node, data);
    }
    return node;
}

void update_stock(tree_node* node, FILE* fp) {
    if (node != NULL) {
        update_stock(node->l_node, fp);
        fprintf(fp, "%d %d %d\n", node->data.ID, node->data.left_stock, node->data.price);
        update_stock(node->r_node, fp);
    }
}

void free_(tree_node *node) {
    if (node == NULL) return; 
    free_(node->l_node);
    free_(node->r_node);
    free(node);
    return; 
}

// 탐색 
tree_node* search_tree(tree_node* node, int id) {
    if (node == NULL || node->data.ID == id) {
        return node;
    } else if (id < node->data.ID) {
        return search_tree(node->l_node, id);
    } else {
        return search_tree(node->r_node, id);
    }
}

// show
void print_tree(tree_node* node, char* buffer) {
    char buf[30]; 
    if (node != NULL) {
        print_tree(node->l_node, buffer);

        P(&(node->data.mutex));
        node->data.readcnt++;
        if((node->data.readcnt) == 1) P(&(node->data.w));
        V(&(node->data.mutex));

        sprintf(buf, "%d %d %d\n", node->data.ID, node->data.left_stock, node->data.price);
        strcat(buffer, buf);

        P(&(node->data.mutex));
        node->data.readcnt--;
        if((node->data.readcnt) == 0) V(&(node->data.w));
        V(&(node->data.mutex));

        print_tree(node->r_node, buffer);
    }
}


// buy
void buy_stock(tree_node* root, int id, int quantity, int *connfd) {
    char buf[MAXLINE];
     sprintf(buf, "buy %d %d\n", id, quantity);
    buf[MAXLINE-1] = '\0';
    tree_node* node = search_tree(root, id);
    if (node != NULL) {
        P(&(node->data.mutex));
        node->data.readcnt++;
        if (node->data.readcnt == 1) P(&(node->data.w));
        V(&(node->data.mutex)); 

        if (node->data.left_stock >= quantity) {
            node->data.left_stock -= quantity;
            strcat(buf, "[buy] success\n");
        } else {
            strcat(buf, "Not enough left stocks\n");
        }
        P(&(node->data.mutex));
        node->data.readcnt--;
        if (node->data.readcnt == 0) V(&(node->data.w));
        V(&(node->data.mutex)); 
    } else {
        strcat(buf, "Invalid stock ID\n");
    }
    Rio_writen(*connfd, buf, MAXLINE);
}

// sell
void sell_stock(tree_node* root, int id, int quantity, int *connfd) {
    char buf[MAXLINE];
    sprintf(buf, "sell %d %d\n", id, quantity);
    buf[MAXLINE-1] = '\0';
    tree_node* node = search_tree(root, id);
    if (node != NULL) {
        P(&(node->data.mutex));
        node->data.readcnt++;
        if (node->data.readcnt == 1) P(&(node->data.w));
        V(&(node->data.mutex)); 

        node->data.left_stock += quantity;
        strcat(buf, "[sell] success\n");

        P(&(node->data.mutex));
        node->data.readcnt--;
        if (node->data.readcnt == 0) V(&(node->data.w));
        V(&(node->data.mutex)); 
    } else {
        strcat(buf, "Invalid stock ID\n");
    }
    Rio_writen(*connfd, buf, MAXLINE);
}

// 상황에 따른 요청 처리함수 
void handle_request(char* request, tree_node* root, int *connfd) {
    char* command = strtok(request, " ");
    char buffer[MAXLINE]; 
    buffer[MAXLINE-1] = '\0';

    if (strcmp(request, "show\n") == 0) {
        memset(buffer, 0, sizeof(buffer)); // buffer 초기화
        strcat(buffer, request);
        print_tree(root, buffer);
        Rio_writen(*connfd, buffer, MAXLINE);
    } 
    else if (strcmp(command, "buy") == 0) {
        int id = atoi(strtok(NULL, " "));
        int quantity = atoi(strtok(NULL, " "));
        buy_stock(root, id, quantity, connfd);
    } 
    else if (strcmp(command, "sell") == 0) {
        int id = atoi(strtok(NULL, " "));
        int quantity = atoi(strtok(NULL, " "));
        sell_stock(root, id, quantity, connfd);
    }
}

int byte_cnt = 0; 

void check_clients(int connfd) {
    int n;
    char buf[MAXLINE]; 
    rio_t rio;

    Rio_readinitb(&rio, connfd);
	while((n = Rio_readlineb(&rio, buf, MAXLINE))!=0){
        byte_cnt += n;
        printf("Server received %d (%d total) bytes on fd %d\n", n, byte_cnt, connfd);

        handle_request(buf, root, &connfd); 
    }
}

void *thread(void *vargp)
{
    Pthread_detach(pthread_self());
    while (1) {
        int connfd = sbuf_remove(&sbuf); /* Remove connfd from buf */
        check_clients(connfd); /* Service client */
        Close(connfd);
    }
}

// ctrl+c로 서버가 종료될 때, 주식 정보 업데이트 
void handle_sigint(int sig) 
{ 
    // File update code
    FILE *fp = fopen("stock.txt", "w"); 
    if (!fp) {
        printf("Error : unable to open file for writing.\n");
        free_(root);
        exit(1); 
    } 
    update_stock(root, fp);
    fclose(fp);
    free_(root);
    printf("\n서버 종료\n");
    exit(0);
} 

int main(int argc, char **argv) 
{
    signal(SIGINT, handle_sigint); // ctrl+c 추가 
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    char client_hostname[MAXLINE], client_port[MAXLINE];
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    FILE *fp = fopen("stock.txt", "r"); 
    if (!fp) {
        printf("Error : file does not exist.\n");
        exit(1); 
    } 
    else {
        int s_id, s_left, s_price; // 번호, 주식 수, 가격 
        while (EOF != fscanf(fp,"%d %d %d", &s_id, &s_left, &s_price)) {
            item data = {s_id, s_left, s_price};
            root = insert_tree(root, data);
        }
    }

    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, SBUFSIZE); 

    for (int i = 0; i < NTHREADS; i++) /* Create worker threads */
        Pthread_create(&tid, NULL, thread, NULL); 

    while (1) {
        clientlen = sizeof(struct sockaddr_storage); 
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
        sbuf_insert(&sbuf, connfd); /* Insert connfd in buffer */
    }
    exit(0);
}
/* $end echoserverimain */