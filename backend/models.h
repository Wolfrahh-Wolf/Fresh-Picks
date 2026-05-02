/*
 * models.h - Fresh Picks: GLOBAL Source of Truth (v4 — Binary Storage Edition)
 * ==============================================================================
 * This header is the SINGLE file every .c file must include.
 * It defines:
 *   1. Constants (string lengths, file paths, max sizes)
 *   2. ALL entity structs (User, Vegetable, Order, FreeItem, DeliveryBoy, AdminCreds)
 *   3. SLL Node structs for each entity (used by utils.c for binary I/O)
 *   4. Pre-existing DS structs (CartNode, QueueNode, DeliveryNode, MinHeap)
 *   5. Function prototypes for utils.c (formerly ds_utils.c)
 *
 *
 * ID FORMAT STANDARD 
 *   Every ID has a PREFIX followed by exactly 4 digits.
 *     Users        → U1001, U1002, ...
 *     Admins       → A1001, A1002, ...
 *     Vegetables   → V1001, V1002, ...
 *     Free Items   → VF1001, VF1002, ...
 *     Orders       → ORD1001, ORD1002, ...
 *     Delivery Boys→ D1001, D1002, ...
 *
 * HOW TO USE: Add this line at the top of every .c file:
 *   #include "models.h"
 *
 * Team: CodeCrafters | Project: Fresh Picks | SDP-1
 */


#ifndef MODELS_H /* MODELS_H include guard */
#define MODELS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ═════════════════════════════════════════════════════════════
   SECTION 1: SIZE CONSTANTS
   ═════════════════════════════════════════════════════════════ */

#define MAX_STR_LEN      128
#define MAX_ID_LEN        20
#define MAX_ADD_LEN      256
#define MAX_LINE_LEN    2048
#define MAX_ORDERS       200
#define MAX_DELIVERY_BOYS 20
#define MAX_CART_ITEMS    50
#define TIMESTAMP_LEN     32 

/* ── Pointer-table sizing constants ── */
#define ID_BASE            1001
#define ADMIN_TABLE_SIZE      5
#define DELIVERY_TABLE_SIZE   4
#define VEG_TABLE_SIZE       48
#define FREE_TABLE_SIZE       2
#define ORDER_TABLE_SIZE    250
#define ORDER_GROWTH_SIZE   100
#define USER_INIT_SIZE      100
#define USER_GROWTH_SIZE     50

/* ═══════════════════════════════════════════════════════════════
   SECTION 2: BINARY FILE PATH CONSTANTS
   ═══════════════════════════════════════════════════════════════ */

#define USERS_FILE        "users.dat"
#define ADMIN_FILE        "admin_creds.dat"
#define PRODUCTS_FILE     "products.dat"
#define ORDERS_FILE       "orders.dat"
#define FREE_INV_FILE     "free_inventory.dat"
#define DELIVERY_FILE     "delivery_boys.dat"
#define SHOP_FILE         "shop_info.txt"
#define CART_DIR          "carts/"
#define DELIMITER         "|"


/* ═════════════════════════════════════════════════════════════
   SECTION 3: OUTPUT MACROS
   ALL C binaries must print results in one of these two formats
   so bridge.py can split on '|' and parse status reliably.
   ═════════════════════════════════════════════════════════════ */

/* Usage: PRINT_SUCCESS("U1001")  →  prints: SUCCESS|U1001  */
#define PRINT_SUCCESS(data)   printf("SUCCESS|%s\n", (data))

/* Usage: PRINT_ERROR("Wrong password")  →  prints: ERROR|Wrong password  */
#define PRINT_ERROR(reason)   printf("ERROR|%s\n",   (reason))


/* ═════════════════════════════════════════════════════════════
   SECTION 4: ENTITY STRUCT DEFINITIONS
   Fixed-size, pointer-free structs that are safe to fread/fwrite.
   NO pointers inside any struct — binary I/O requires flat layout.
   ═════════════════════════════════════════════════════════════ */

/* ─────────────────────────────────────────────
   STRUCT: AdminCreds
   DB Format: admin_id|username|password|admin_name|email
   ID Format: A1001, A1002, ...
   ───────────────────────────────────────────── */
typedef struct {
    char admin_id[MAX_ID_LEN];
    char username[MAX_STR_LEN];
    char password[MAX_STR_LEN];
    char admin_name[MAX_STR_LEN];
    char email[MAX_STR_LEN];
} AdminCreds;


/* ─────────────────────────────────────────────
   STRUCT: User
   DB Format: user_id|username|password|full_name|email|phone|address
   ID Format: U1001, U1002, ...
   ───────────────────────────────────────────── */
typedef struct {
    char user_id[MAX_ID_LEN];
    char username[MAX_STR_LEN];
    char password[MAX_STR_LEN];
    char full_name[MAX_STR_LEN];
    char email[MAX_STR_LEN];
    char phone[MAX_STR_LEN];
    char address[MAX_ADD_LEN];
} User;

/* ─────────────────────────────────────────────
   STRUCT: Vegetable
   DB Format: veg_id|category|name|stock_g|price_per_1000g|tag|validity_days
   ID Format: V1001, V1002, ...
   ───────────────────────────────────────────── */
typedef struct {
    char  veg_id[MAX_ID_LEN];
    char  category[MAX_STR_LEN];
    char  name[MAX_STR_LEN];
    int   stock_g;
    float price_per_1000g;
    char  tag[MAX_STR_LEN];
    int   validity_days;
} Vegetable;

/* ─────────────────────────────────────────────
   STRUCT: FreeItem
   DB Format: vf_id|name|stock_g|min_trigger_amt|free_qty_g
   ID Format: VF1001, VF1002, ...
   ───────────────────────────────────────────── */
typedef struct {
    char  vf_id[MAX_ID_LEN];
    char  name[MAX_STR_LEN];
    int   stock_g;
    float min_trigger_amt;
    int   free_qty_g;
} FreeItem;

/* ─────────────────────────────────────────────
   STRUCT: DeliveryBoy
   DB Format: boy_id|name|phone|vehicle_no|is_active|last_assigned
   ID Format: D1001, D1002, ...
   ───────────────────────────────────────────── */
typedef struct {
    char boy_id[MAX_ID_LEN];
    char name[MAX_STR_LEN];
    char phone[MAX_STR_LEN];
    char vehicle_no[MAX_STR_LEN];
    int  is_active;
    int  last_assigned;
} DeliveryBoy;

/* ─────────────────────────────────────────────
   STRUCT: Order
   One row loaded from orders.dat
   DB Format: order_id|user_id|total|slot|boy_id|status|timestamp|items_string
   ID Format: ORD1001, ORD1002, ...

   items_string format (normalised in v4):
     "V1001:Onion:500:40.00,VF1001:Curry Leaves:50:0.00"
     Each token: veg_id:name:qty_g:price_at_order
     price_at_order is a SNAPSHOT — never changes after placement.
   ───────────────────────────────────────────── */
typedef struct {
    char  order_id[MAX_ID_LEN];
    char  user_id[MAX_ID_LEN];
    float total_amount;
    char  delivery_slot[MAX_STR_LEN];
    char  delivery_boy_id[MAX_ID_LEN];
    char  status[MAX_STR_LEN];        
    char  timestamp[TIMESTAMP_LEN];   /* "YYYY-MM-DD HH:MM:SS"          */
    char  items_string[MAX_LINE_LEN];
    int   slot_priority;              /* 1=Morning, 2=Afternoon, 3=Evening */
} Order;

/* ─────────────────────────────────────────────
   ANALYTICS STRUCT DEFINITIONS
   ───────────────────────────────────────────── */
typedef struct {
    float total_revenue;
    int   total_orders;
    int   orders_placed;
    int   orders_out;
    int   orders_delivered;
    int   orders_cancelled;
    int   slot_morning;
    int   slot_afternoon;
    int   slot_evening;
} OrderMetrics;

typedef struct {
    float total_stock_kg;
    int   low_stock_items;
} InventoryMetrics;

typedef struct {
    int total_users;
    int active_delivery_boys;
    int inactive_delivery_boys;
} StaffMetrics;


/* ═══════════════════════════════════════════════════════════════
   SECTION 5: SLL NODE STRUCTS
   ═══════════════════════════════════════════════════════════════ */

typedef struct UserNode {
    User             data;
    struct UserNode *next;
} UserNode;

typedef struct VegNode {
    Vegetable       data;
    struct VegNode *next;
} VegNode;

typedef struct OrderNode {
    Order             data;
    struct OrderNode *next;
} OrderNode;

typedef struct FreeItemNode {
    FreeItem             data;
    struct FreeItemNode *next;
} FreeItemNode;

typedef struct DeliveryBoyNode {
    DeliveryBoy             data;
    struct DeliveryBoyNode *next;
} DeliveryBoyNode;

typedef struct AdminNode {
    AdminCreds       data;
    struct AdminNode *next;
} AdminNode;



/* ═══════════════════════════════════════════════════════════════
   SECTION 6: RUNTIME DATA STRUCTURE NODE / CONTAINER STRUCTS
   DLL (cart), Queue (order processing), CLL (delivery), Min-Heap.
   ═══════════════════════════════════════════════════════════════ */

/* CartNode: one item in a user's cart (Doubly Linked List node) */
typedef struct CartNode {
    char  veg_id[MAX_ID_LEN];
    char  name[MAX_STR_LEN];
    int   qty_g;
    float price_per_1000g;
    float item_total;
    int   is_free;
    struct CartNode *prev;
    struct CartNode *next;
} CartNode;

/* QueueNode: one Order in the FIFO processing queue */
typedef struct QueueNode {
    Order order;
    struct QueueNode *next;
} QueueNode;

/* OrderQueue: front/rear pointers and count for the FIFO queue */
typedef struct {
    QueueNode *front;
    QueueNode *rear;
    int        size;
} OrderQueue;

/* DeliveryNode: one active DeliveryBoy in the Circular Linked List */
typedef struct DeliveryNode {
    DeliveryBoy         boy;
    struct DeliveryNode *next;
} DeliveryNode;

/* MinHeap: flat-array binary heap ordered by slot_priority (ascending) */
/* 1=Morning, 2=Afternoon, 3=Evening */
typedef struct {
    Order data[MAX_ORDERS];
    int   size;
} MinHeap;

/* ═══════════════════════════════════════════════════════════════
   SECTION 7: FUNCTION PROTOTYPES FOR utils.c
   ═══════════════════════════════════════════════════════════════ */

/* ── SLL Load / Save / Free (Binary I/O Abstraction Layer) ── */
UserNode*        load_user_sll(void);
void             save_user_sll(UserNode* head);
void             free_user_sll(UserNode* head);

VegNode*         load_veg_sll(void);
void             save_veg_sll(VegNode* head);
void             free_veg_sll(VegNode* head);

OrderNode*       load_order_sll(void);
void             save_order_sll(OrderNode* head);
void             free_order_sll(OrderNode* head);

FreeItemNode*    load_free_item_sll(void);
void             save_free_item_sll(FreeItemNode* head);
void             free_free_item_sll(FreeItemNode* head);

DeliveryBoyNode* load_delivery_boy_sll(void);
void             save_delivery_boy_sll(DeliveryBoyNode* head);
void             free_delivery_boy_sll(DeliveryBoyNode* head);

AdminNode*       load_admin_sll(void);
void             save_admin_sll(AdminNode* head);
void             free_admin_sll(AdminNode* head);

/* ── SLL Utility helpers ── */
int  sll_count_orders(OrderNode* head);
int  sll_count_users(UserNode* head);

int            get_index_from_id(const char* id);
UserNode**     build_user_table(UserNode* head, int* current_max_size);
AdminNode**    build_admin_table(AdminNode* head);
VegNode**      build_veg_table(VegNode* head, int* table_size);
FreeItemNode** build_free_table(FreeItemNode* head, int* table_size);
OrderNode**    build_order_table(OrderNode* head, int* current_max_size);

/* ── Doubly Linked List (Cart) ── */
CartNode* dll_create_node(const char* veg_id, const char* name,
                          int qty_g, float price_per_1000g, int is_free);
void      dll_append(CartNode** head, CartNode* new_node);
void      dll_update_or_append(CartNode** head, const char* veg_id,
                               const char* name, int qty_g,
                               float price_per_1000g, int is_free);
void      dll_remove(CartNode** head, const char* veg_id);
float     dll_get_total(CartNode* head);
void      dll_free_all(CartNode* head);

/* ── Standard Queue (Order Processing) ── */
void      queue_init(OrderQueue* q);
void      queue_enqueue(OrderQueue* q, Order o);
int       queue_dequeue(OrderQueue* q, Order* out);
void      queue_free(OrderQueue* q);

/* ── Circular Linked List (Delivery Boys — built from SLL at runtime) ── */
DeliveryNode* cll_build_from_sll(DeliveryBoyNode* sll_head);
int           cll_assign_delivery(DeliveryNode* head, DeliveryBoy* out_boy,
                                  DeliveryBoyNode* sll_head);
void          cll_free(DeliveryNode* head);

/* ── Min-Heap (Admin Priority Queue) ── */
void heap_swap(MinHeap* h, int i, int j);
void heap_heapify_up(MinHeap* h, int idx);
void heap_heapify_down(MinHeap* h, int idx);
void heap_insert(MinHeap* h, Order o);
int  heap_extract_min(MinHeap* h, Order* out);


#endif /* End of MODELS_H include guard */
