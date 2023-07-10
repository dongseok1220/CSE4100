/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

/* 바이너리 트리 구현 부분 */
typedef struct item {
    int ID;
    int left_stock;
    int price;
    // int readcnt;
    // sem_t mutex; 
    // thread에서 사용하므로 필요없음
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
// 합쳐서 보내야할듯?? 
void print_tree(tree_node* node, char* buffer) {
    char buf[30]; 
    if (node != NULL) {
        print_tree(node->l_node, buffer);
        sprintf(buf, "%d %d %d\n", node->data.ID, node->data.left_stock, node->data.price);
        strcat(buffer, buf);
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
        if (node->data.left_stock >= quantity) {
            node->data.left_stock -= quantity;
            strcat(buf, "[buy] success\n");
        } else {
            strcat(buf, "Not enough left stocks\n");
        }
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
        node->data.left_stock += quantity;
        strcat(buf, "[sell] success\n");
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


/* 강의 자료 참고 작성 */
typedef struct {
    int maxfd;
    fd_set read_set;
    fd_set ready_set;
    int nready;
    int maxi;
    int clientfd[FD_SETSIZE];
    rio_t clientrio[FD_SETSIZE];
} pool; 

int byte_cnt = 0; 

void init_pool(int listenfd, pool *p) {
    int i;
    p->maxi = -1;
    for (i=0; i< FD_SETSIZE; i++) 
        p->clientfd[i] = -1;
    
    p->maxfd = listenfd;
    FD_ZERO(&p->read_set);
    FD_SET(listenfd, &p->read_set);
}

void add_client (int connfd, pool *p) {
    int i; 
    p->nready--;
    for (i=0; i<FD_SETSIZE; i++) {
        if (p->clientfd[i] < 0 ) {
            p->clientfd[i] = connfd; 
            Rio_readinitb(&p->clientrio[i], connfd);

            FD_SET(connfd, &p->read_set);

            if (connfd > p->maxfd) 
                p->maxfd = connfd; 
            if (i > p->maxi) 
                p->maxi = i;
            break;
        }
    }
    if (i == FD_SETSIZE) 
        app_error("add_client error: Too many clients"); 
}

void check_clients (pool *p) {
    int i, connfd, n;
    char buf[MAXLINE]; 
    rio_t rio;

    for (i=0; (i <= p->maxi) && (p->nready > 0); i++) {
        connfd = p->clientfd[i];
        rio = p->clientrio[i];

        if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) {
            p->nready--;
            if ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0 ) {
                byte_cnt += n;
                printf("Server received %d (%d total) bytes on fd %d\n", n, byte_cnt, connfd);
                
                // printf("%s\n", buf); 
                // Rio_writen(connfd, buf, n); 

                handle_request(buf, root, &connfd); 
            }
            else {
                Close(connfd);
                FD_CLR(connfd, &p->read_set);
                p->clientfd[i] = -1; 
            }
        }
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
    static pool pool; 

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

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    listenfd = Open_listenfd(argv[1]);
    init_pool(listenfd, &pool); 

    while (1) {
        pool.ready_set = pool.read_set; 
        pool.nready = Select(pool.maxfd+1, &pool.ready_set, NULL,NULL,NULL); 
    
        if (FD_ISSET(listenfd, &pool.ready_set)) {
            clientlen = sizeof(struct sockaddr_storage); 
            connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
            Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
			printf("Connected to (%s, %s)\n", client_hostname, client_port);
            add_client(connfd, &pool); 
        }
        check_clients(&pool);
    }
    exit(0);
}
/* $end echoserverimain */