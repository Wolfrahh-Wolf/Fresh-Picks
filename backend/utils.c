/*
 * utils.c - Fresh Picks: Data Structure & Binary I/O Utility Library 
 */



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "models.h"
#ifdef _WIN32
    #include <io.h>
    #include <sys/locking.h>

    #define LOCK_FILE(fp)   _locking(_fileno(fp), _LK_LOCK, 1)
    #define UNLOCK_FILE(fp) _locking(_fileno(fp), _LK_UNLCK, 1)

#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/file.h>

    #define LOCK_FILE(fp)   flock(fileno(fp), LOCK_EX)
    #define UNLOCK_FILE(fp) flock(fileno(fp), LOCK_UN)

#endif

 /* ══════════════════════════════════════════════════════════════
   WINDOWS File Locking Macros
   ══════════════════════════════════════════════════════════════ */
/* ══════════════════════════════════════════════════════════════
   INTERNAL HELPER MACROS (used only inside this file)

   SLL_LOAD_OPEN(filepath, fp)
     Opens the .dat file for reading. Returns NULL on failure.

   SLL_SAVE_OPEN(filepath, fp)
     Opens the .dat file for writing (truncates). Returns on failure.
   ══════════════════════════════════════════════════════════════ */

#define SLL_LOAD_OPEN(filepath, fp)                     \
    do {                                                \
        (fp) = fopen((filepath), "rb");                 \
        if (!(fp)) return NULL;                         \
        LOCK_FILE(fp);                                  \
    } while (0)

#define SLL_SAVE_OPEN(filepath, fp)                     \
    do {                                                \
        (fp) = fopen((filepath), "wb");                 \
        if (!(fp)) return;                              \
        LOCK_FILE(fp);                                  \
    } while (0)

#define SLL_CLOSE(fp)                                   \
    do {                                                \
        UNLOCK_FILE(fp);                                \
        fclose(fp);                                     \
    } while (0)


/* ══════════════════════════════════════════════════════════════
   DATA STRUCTURE 5A: USER SLL  —  Persistent store: users.dat
   ══════════════════════════════════════════════════════════════ */

/* Read users.dat into a SLL; caller must free */
UserNode* load_user_sll(void) {
    FILE* fp;
    SLL_LOAD_OPEN(USERS_FILE, fp);

    UserNode* head = NULL;
    UserNode* tail = NULL;
    User      buf;

    while (fread(&buf, sizeof(User), 1, fp) == 1) {
        UserNode* node = (UserNode*)malloc(sizeof(UserNode));
        if (!node) break;

        node->data = buf;
        node->next = NULL;

        if (!head) { head = node; tail = node; }
        else        { tail->next = node; tail = node; }
    }

    SLL_CLOSE(fp);
    return head;
}

/* Overwrite users.dat with the current SLL */
void save_user_sll(UserNode* head) {
    FILE* fp;
    SLL_SAVE_OPEN(USERS_FILE, fp);

    UserNode* curr = head;
    while (curr) {
        fwrite(&curr->data, sizeof(User), 1, fp);
        curr = curr->next;
    }

    SLL_CLOSE(fp);
}

/* Walk and free every heap-allocated UserNode */
void free_user_sll(UserNode* head) {
    UserNode* curr = head;
    while (curr) {
        UserNode* next = curr->next;
        free(curr);
        curr = next;
    }
}

/* ══════════════════════════════════════════════════════════════
   DATA STRUCTURE 5B: VEGETABLE SLL  —  Persistent store: products.dat
   ══════════════════════════════════════════════════════════════ */

/* Read products.dat into a SLL; caller must free */
VegNode* load_veg_sll(void) {
    FILE* fp;
    SLL_LOAD_OPEN(PRODUCTS_FILE, fp);

    VegNode*  head = NULL;
    VegNode*  tail = NULL;
    Vegetable buf;

    while (fread(&buf, sizeof(Vegetable), 1, fp) == 1) {
        VegNode* node = (VegNode*)malloc(sizeof(VegNode));
        if (!node) break;

        node->data = buf;
        node->next = NULL;

        if (!head) { head = node; tail = node; }
        else        { tail->next = node; tail = node; }
    }

    SLL_CLOSE(fp);
    return head;
}

/* Overwrite products.dat with the current SLL */
void save_veg_sll(VegNode* head) {
    FILE* fp;
    SLL_SAVE_OPEN(PRODUCTS_FILE, fp);

    VegNode* curr = head;
    while (curr) {
        fwrite(&curr->data, sizeof(Vegetable), 1, fp);
        curr = curr->next;
    }

    SLL_CLOSE(fp);
}

/* Walk and free every heap-allocated VegNode */
void free_veg_sll(VegNode* head) {
    VegNode* curr = head;
    while (curr) {
        VegNode* next = curr->next;
        free(curr);
        curr = next;
    }
}


/* ══════════════════════════════════════════════════════════════
   DATA STRUCTURE 5C: ORDER SLL  —  Persistent store: orders.dat
   ══════════════════════════════════════════════════════════════ */

/* Read orders.dat into a SLL; caller must free */
OrderNode* load_order_sll(void) {
    FILE* fp;
    SLL_LOAD_OPEN(ORDERS_FILE, fp);

    OrderNode* head = NULL;
    OrderNode* tail = NULL;
    Order      buf;

    while (fread(&buf, sizeof(Order), 1, fp) == 1) {
        OrderNode* node = (OrderNode*)malloc(sizeof(OrderNode));
        if (!node) break;

        node->data = buf;
        node->next = NULL;

        if (!head) { head = node; tail = node; }
        else        { tail->next = node; tail = node; }
    }

    SLL_CLOSE(fp);
    return head;
}

/* Overwrite orders.dat with the current SLL */
void save_order_sll(OrderNode* head) {
    FILE* fp;
    SLL_SAVE_OPEN(ORDERS_FILE, fp);

    OrderNode* curr = head;
    while (curr) {
        fwrite(&curr->data, sizeof(Order), 1, fp);
        curr = curr->next;
    }

    SLL_CLOSE(fp);
}

/* Walk and free every heap-allocated OrderNode */
void free_order_sll(OrderNode* head) {
    OrderNode* curr = head;
    while (curr) {
        OrderNode* next = curr->next;
        free(curr);
        curr = next;
    }
}


/* ══════════════════════════════════════════════════════════════
   DATA STRUCTURE 5D: FREE ITEM SLL  —  Persistent store: free_inventory.dat
   ══════════════════════════════════════════════════════════════ */

/* Read free_inventory.dat into a SLL; caller must free */
FreeItemNode* load_free_item_sll(void) {
    FILE* fp;
    SLL_LOAD_OPEN(FREE_INV_FILE, fp);

    FreeItemNode* head = NULL;
    FreeItemNode* tail = NULL;
    FreeItem      buf;

    while (fread(&buf, sizeof(FreeItem), 1, fp) == 1) {
        FreeItemNode* node = (FreeItemNode*)malloc(sizeof(FreeItemNode));
        if (!node) break;

        node->data = buf;
        node->next = NULL;

        if (!head) { head = node; tail = node; }
        else        { tail->next = node; tail = node; }
    }

    SLL_CLOSE(fp);
    return head;
}

/* Overwrite free_inventory.dat with the current SLL */
void save_free_item_sll(FreeItemNode* head) {
    FILE* fp;
    SLL_SAVE_OPEN(FREE_INV_FILE, fp);

    FreeItemNode* curr = head;
    while (curr) {
        fwrite(&curr->data, sizeof(FreeItem), 1, fp);
        curr = curr->next;
    }

    SLL_CLOSE(fp);
}

/* Walk and free every heap-allocated FreeItemNode */
void free_free_item_sll(FreeItemNode* head) {
    FreeItemNode* curr = head;
    while (curr) {
        FreeItemNode* next = curr->next;
        free(curr);
        curr = next;
    }
}


/* ══════════════════════════════════════════════════════════════
   DATA STRUCTURE 5E: DELIVERY BOY SLL  —  Persistent store: delivery_boys.dat
   ══════════════════════════════════════════════════════════════ */

/* Read delivery_boys.dat into a SLL; caller must free */
DeliveryBoyNode* load_delivery_boy_sll(void) {
    FILE* fp;
    SLL_LOAD_OPEN(DELIVERY_FILE, fp);

    DeliveryBoyNode* head = NULL;
    DeliveryBoyNode* tail = NULL;
    DeliveryBoy      buf;

    while (fread(&buf, sizeof(DeliveryBoy), 1, fp) == 1) {
        DeliveryBoyNode* node = (DeliveryBoyNode*)malloc(sizeof(DeliveryBoyNode));
        if (!node) break;

        node->data = buf;
        node->next = NULL;

        if (!head) { head = node; tail = node; }
        else        { tail->next = node; tail = node; }
    }

    SLL_CLOSE(fp);
    return head;
}

/* Overwrite delivery_boys.dat with the current SLL */
void save_delivery_boy_sll(DeliveryBoyNode* head) {
    FILE* fp;
    SLL_SAVE_OPEN(DELIVERY_FILE, fp);

    DeliveryBoyNode* curr = head;
    while (curr) {
        fwrite(&curr->data, sizeof(DeliveryBoy), 1, fp);
        curr = curr->next;
    }

    SLL_CLOSE(fp);
}

/* Walk and free every heap-allocated DeliveryBoyNode */
void free_delivery_boy_sll(DeliveryBoyNode* head) {
    DeliveryBoyNode* curr = head;
    while (curr) {
        DeliveryBoyNode* next = curr->next;
        free(curr);
        curr = next;
    }
}


/* ══════════════════════════════════════════════════════════════
   DATA STRUCTURE 5F: ADMIN SLL  —  Persistent store: admin_creds.dat
   ══════════════════════════════════════════════════════════════ */

/* Read admin_creds.dat into a SLL; caller must free */
AdminNode* load_admin_sll(void) {
    FILE* fp;
    SLL_LOAD_OPEN(ADMIN_FILE, fp);

    AdminNode*  head = NULL;
    AdminNode*  tail = NULL;
    AdminCreds  buf;

    while (fread(&buf, sizeof(AdminCreds), 1, fp) == 1) {
        AdminNode* node = (AdminNode*)malloc(sizeof(AdminNode));
        if (!node) break;

        node->data = buf;
        node->next = NULL;

        if (!head) { head = node; tail = node; }
        else        { tail->next = node; tail = node; }
    }

    SLL_CLOSE(fp);
    return head;
}

/* Overwrite admin_creds.dat with the current SLL */
void save_admin_sll(AdminNode* head) {
    FILE* fp;
    SLL_SAVE_OPEN(ADMIN_FILE, fp);

    AdminNode* curr = head;
    while (curr) {
        fwrite(&curr->data, sizeof(AdminCreds), 1, fp);
        curr = curr->next;
    }

    SLL_CLOSE(fp);
}

/* Walk and free every heap-allocated AdminNode */
void free_admin_sll(AdminNode* head) {
    AdminNode* curr = head;
    while (curr) {
        AdminNode* next = curr->next;
        free(curr);
        curr = next;
    }
}

/* ══════════════════════════════════════════════════════════════
   SLL UTILITY HELPERS
   ══════════════════════════════════════════════════════════════ */

/* Return the number of nodes in an OrderNode SLL */
int sll_count_orders(OrderNode* head) {
    int count = 0;
    OrderNode* curr = head;
    while (curr) { count++; curr = curr->next; }
    return count;
}

/* Return the number of nodes in a UserNode SLL */
int sll_count_users(UserNode* head) {
    int count = 0;
    UserNode* curr = head;
    while (curr) { count++; curr = curr->next; }
    return count;
}

/* Skip alphabetical prefix, return numeric part minus ID_BASE */
int get_index_from_id(const char* id) {
    while (*id && isalpha((unsigned char)*id)) id++;  /* advance past prefix chars */
    return atoi(id) - ID_BASE;
}

/* Build a direct-index pointer table from the User SLL;
   grows by USER_GROWTH_SIZE if needed and updates *current_max_size */
UserNode** build_user_table(UserNode* head, int* current_max_size) {
    *current_max_size = USER_INIT_SIZE;

    UserNode** table = (UserNode**)calloc(*current_max_size, sizeof(UserNode*));
    if (!table) return NULL;

    UserNode* curr = head;
    while (curr) {
        int idx = get_index_from_id(curr->data.user_id);

        if (idx >= *current_max_size) {
            int new_size = *current_max_size + USER_GROWTH_SIZE;
            UserNode** tmp = (UserNode**)realloc(table, new_size * sizeof(UserNode*));
            if (!tmp) { free(table); return NULL; }
            /* Zero-initialise the newly added slots */
            memset(tmp + *current_max_size, 0, USER_GROWTH_SIZE * sizeof(UserNode*));
            table = tmp;
            *current_max_size = new_size;
        }

        table[idx] = curr;
        curr = curr->next;
    }

    return table;
}

/* Build a fixed direct-index pointer table from the Admin SLL */
AdminNode** build_admin_table(AdminNode* head) {
    AdminNode** table = (AdminNode**)calloc(ADMIN_TABLE_SIZE, sizeof(AdminNode*));
    if (!table) return NULL;

    AdminNode* curr = head;
    while (curr) {
        int idx = get_index_from_id(curr->data.admin_id);
        if (idx >= 0 && idx < ADMIN_TABLE_SIZE)
            table[idx] = curr;
        curr = curr->next;
    }

    return table;
}

/* Build a fixed direct-index pointer table from the Vegetable SLL */
VegNode** build_veg_table(VegNode* head, int* table_size) {
    *table_size = VEG_TABLE_SIZE;
    VegNode** table = (VegNode**)calloc(*table_size, sizeof(VegNode*));
    if (!table) return NULL;

    VegNode* curr = head;
    while (curr) {
        int idx = get_index_from_id(curr->data.veg_id);
        if (idx >= 0 && idx < VEG_TABLE_SIZE)
            table[idx] = curr;
        curr = curr->next;
    }

    return table;
}

/* Build a fixed direct-index pointer table from the FreeItem SLL */
FreeItemNode** build_free_table(FreeItemNode* head, int* table_size) {
    *table_size = FREE_TABLE_SIZE; // Assign the constant to the pointer
    FreeItemNode** table = (FreeItemNode**)calloc(*table_size, sizeof(FreeItemNode*));
    if (!table) return NULL;

    FreeItemNode* curr = head;
    while (curr) {
        int idx = get_index_from_id(curr->data.vf_id);
        if (idx >= 0 && idx < FREE_TABLE_SIZE)
            table[idx] = curr;
        curr = curr->next;
    }

    return table;
}

/* Build a direct-index pointer table from the Order SLL;
   grows by ORDER_GROWTH_SIZE if needed and updates *current_max_size */
OrderNode** build_order_table(OrderNode* head, int* current_max_size) {
    *current_max_size = ORDER_TABLE_SIZE;

    OrderNode** table = (OrderNode**)calloc(*current_max_size, sizeof(OrderNode*));
    if (!table) return NULL;

    OrderNode* curr = head;
    while (curr) {
        int idx = get_index_from_id(curr->data.order_id);

        if (idx >= *current_max_size) {
            int new_size = *current_max_size + ORDER_GROWTH_SIZE;
            OrderNode** tmp = (OrderNode**)realloc(table, new_size * sizeof(OrderNode*));
            if (!tmp) { free(table); return NULL; }
            /* Zero-initialise the newly added slots */
            memset(tmp + *current_max_size, 0, ORDER_GROWTH_SIZE * sizeof(OrderNode*));
            table = tmp;
            *current_max_size = new_size;
        }

        table[idx] = curr;
        curr = curr->next;
    }

    return table;
}


/* ══════════════════════════════════════════════════════════════
   DATA STRUCTURE 1: DOUBLY LINKED LIST (Cart)
   ══════════════════════════════════════════════════════════════ */

/* Allocate and initialise a new CartNode; returns NULL on failure */
CartNode* dll_create_node(const char* veg_id, const char* name,
                          int qty_g, float price_per_1000g, int is_free) {
    CartNode* node = (CartNode*)malloc(sizeof(CartNode));
    if (!node) return NULL;

    strncpy(node->veg_id, veg_id, MAX_ID_LEN  - 1);
    strncpy(node->name,   name,   MAX_STR_LEN - 1);
    node->veg_id[MAX_ID_LEN  - 1] = '\0';
    node->name  [MAX_STR_LEN - 1] = '\0';

    node->qty_g           = qty_g;
    node->price_per_1000g = price_per_1000g;
    node->item_total      = (qty_g / 1000.0f) * price_per_1000g;
    node->is_free         = is_free;
    node->prev            = NULL;
    node->next            = NULL;

    return node;
}

/* Append new_node at the tail of the DLL */
void dll_append(CartNode** head, CartNode* new_node) {
    if (!*head) { *head = new_node; return; }

    CartNode* curr = *head;
    while (curr->next) curr = curr->next;

    curr->next     = new_node;
    new_node->prev = curr;
}

/* Set qty for an existing cart item, or append if not found */
void dll_update_or_append(CartNode** head, const char* veg_id, const char* name,
                          int qty_g, float price_per_1000g, int is_free) {
    CartNode* curr = *head;
    while (curr) {
        if (strcmp(curr->veg_id, veg_id) == 0) {
            curr->qty_g      = qty_g;
            curr->item_total = (qty_g / 1000.0f) * price_per_1000g;
            return;
        }
        curr = curr->next;
    }

    CartNode* node = dll_create_node(veg_id, name, qty_g, price_per_1000g, is_free);
    if (node) dll_append(head, node);
}

/* Unlink and free the CartNode matching veg_id */
void dll_remove(CartNode** head, const char* veg_id) {
    CartNode* curr = *head;
    while (curr) {
        if (strcmp(curr->veg_id, veg_id) == 0) {
            if (curr->prev) curr->prev->next = curr->next;
            if (curr->next) curr->next->prev = curr->prev;
            if (curr == *head) *head = curr->next;
            free(curr);
            return;
        }
        curr = curr->next;
    }
}

/* Sum item_total across all CartNodes and return grand total */
float dll_get_total(CartNode* head) {
    float total = 0.0f;
    CartNode* curr = head;
    while (curr) { total += curr->item_total; curr = curr->next; }
    return total;
}

/* Release memory for every node in the cart DLL */
void dll_free_all(CartNode* head) {
    CartNode* curr = head;
    while (curr) {
        CartNode* next = curr->next;
        free(curr);
        curr = next;
    }
}


/* ══════════════════════════════════════════════════════════════
   DATA STRUCTURE 2: STANDARD QUEUE (FIFO Order Processing)
   ══════════════════════════════════════════════════════════════ */

/* Zero-initialise an OrderQueue before first use */
void queue_init(OrderQueue* q) {
    q->front = NULL;
    q->rear  = NULL;
    q->size  = 0;
}

/* Append an Order at the rear of the queue */
void queue_enqueue(OrderQueue* q, Order o) {
    QueueNode* node = (QueueNode*)malloc(sizeof(QueueNode));
    if (!node) return;

    node->order = o;
    node->next  = NULL;

    if (!q->rear) { q->front = node; q->rear = node; }
    else          { q->rear->next = node; q->rear = node; }

    q->size++;
}

/* Remove and return the front Order; returns 1 on success, 0 if empty */
int queue_dequeue(OrderQueue* q, Order* out) {
    if (!q->front) return 0;

    QueueNode* temp = q->front;
    *out     = temp->order;
    q->front = temp->next;
    if (!q->front) q->rear = NULL;

    free(temp);
    q->size--;
    return 1;
}

/* Drain and free all remaining nodes in the queue */
void queue_free(OrderQueue* q) {
    Order dummy;
    while (queue_dequeue(q, &dummy));
}


/* ══════════════════════════════════════════════════════════════
   DATA STRUCTURE 3: CIRCULAR LINKED LIST (Delivery Allocation)
   ══════════════════════════════════════════════════════════════ */

/* Build a CLL of active delivery boys from an in-memory SLL */
DeliveryNode* cll_build_from_sll(DeliveryBoyNode* sll_head) {
    DeliveryNode* head = NULL;
    DeliveryNode* tail = NULL;

    DeliveryBoyNode* curr = sll_head;
    while (curr) {
        if (!curr->data.is_active) { curr = curr->next; continue; }

        DeliveryNode* node = (DeliveryNode*)malloc(sizeof(DeliveryNode));
        if (!node) { curr = curr->next; continue; }

        node->boy  = curr->data;
        node->next = NULL;

        if (!head) {
            head       = node;
            tail       = node;
            node->next = head;   /* circle of one */
        } else {
            tail->next = node;
            tail       = node;
            tail->next = head;   /* close the circle */
        }

        curr = curr->next;
    }

    return head;
}

/* Round-robin pick of next active boy; persists updated flags */
int cll_assign_delivery(DeliveryNode* head, DeliveryBoy* out_boy,
                        DeliveryBoyNode* sll_head) {
    if (!head) return 0;

    /* Count CLL nodes to bound traversal — CLL has no NULL terminator */
    int count = 0;
    DeliveryNode* walker = head;
    do { count++; walker = walker->next; } while (walker != head);

    DeliveryNode* curr     = head;
    DeliveryNode* chosen   = NULL;
    DeliveryNode* prev_boy = NULL;
    int found = 0;

    for (int i = 0; i < count; i++) {
        if (curr->boy.last_assigned == 1) {
            prev_boy = curr;
            chosen   = curr->next;   /* round-robin: NEXT boy gets this order */
            found    = 1;
            break;
        }
        curr = curr->next;
    }

    if (!found) chosen = head;   /* first-order edge case: no flag set yet */

    if (prev_boy) prev_boy->boy.last_assigned = 0;
    chosen->boy.last_assigned = 1;
    *out_boy = chosen->boy;

    /* Mirror flag changes into the persistent SLL before saving */
    DeliveryBoyNode* s = sll_head;
    while (s) {
        if (strcmp(s->data.boy_id, chosen->boy.boy_id) == 0)
            s->data.last_assigned = 1;
        else if (prev_boy && strcmp(s->data.boy_id, prev_boy->boy.boy_id) == 0)
            s->data.last_assigned = 0;
        s = s->next;
    }

    save_delivery_boy_sll(sll_head);
    return 1;
}

/* Free all CLL nodes by counting first (no NULL terminator) */
void cll_free(DeliveryNode* head) {
    if (!head) return;

    int count = 0;
    DeliveryNode* curr = head;
    do { count++; curr = curr->next; } while (curr != head);

    curr = head;
    for (int i = 0; i < count; i++) {
        DeliveryNode* next = curr->next;
        free(curr);
        curr = next;
    }
}


/* ══════════════════════════════════════════════════════════════
   DATA STRUCTURE 4: MIN-HEAP (Admin Priority Queue)
   Array-mapped binary heap; slot_priority 1=Morning (most urgent).
   ══════════════════════════════════════════════════════════════ */

/* Swap two Order elements within the heap array */
void heap_swap(MinHeap* h, int i, int j) {
    Order temp  = h->data[i];
    h->data[i]  = h->data[j];
    h->data[j]  = temp;
}

/* Bubble a newly inserted element up to its correct position */
void heap_heapify_up(MinHeap* h, int idx) {
    while (idx > 0) {
        int parent = (idx - 1) / 2;
        if (h->data[idx].slot_priority < h->data[parent].slot_priority) {
            heap_swap(h, idx, parent);
            idx = parent;
        } else {
            break;
        }
    }
}

/* Sink the root element down after an extract-min */
void heap_heapify_down(MinHeap* h, int idx) {
    while (1) {
        int left     = 2 * idx + 1;
        int right    = 2 * idx + 2;
        int smallest = idx;

        if (left  < h->size && h->data[left ].slot_priority < h->data[smallest].slot_priority)
            smallest = left;
        if (right < h->size && h->data[right].slot_priority < h->data[smallest].slot_priority)
            smallest = right;

        if (smallest != idx) { heap_swap(h, idx, smallest); idx = smallest; }
        else break;
    }
}

/* Add an Order to the heap in O(log n) */
void heap_insert(MinHeap* h, Order o) {
    if (h->size >= MAX_ORDERS) return;
    h->data[h->size] = o;
    heap_heapify_up(h, h->size);
    h->size++;
}

/* Remove and return the most-urgent Order; returns 1 on success, 0 if empty */
int heap_extract_min(MinHeap* h, Order* out) {
    if (h->size == 0) return 0;

    *out            = h->data[0];
    h->data[0]      = h->data[h->size - 1];
    h->size--;

    if (h->size > 0) heap_heapify_down(h, 0);
    return 1;
}
