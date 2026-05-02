/*
 * analytics.c - Fresh Picks: Admin Analytics Dashboard (v5 — Direct Indexing)
 * =============================================================================
 * Called by Flask via subprocess.run(): ./analytics <command>
 *
 * ALL persistent I/O is delegated to utils.c (load/free SLL functions).
 * Direct fopen / fgets / strtok / fprintf on data files is STRICTLY FORBIDDEN.
 *
 * OUTPUT CONTRACT:
 *   First line : SUCCESS   (no pipe, no extra text)
 *   Every line after: key|value
 *   On failure : ERROR|reason
 *
 * COMMANDS (argv[1]):
 *   get_analytics   — Compute and print all dashboard metrics
 *
 * METRICS EMITTED (in order):
 *   total_revenue          float  — sum of total_amount across all orders
 *   total_orders           int    — count of all orders
 *   avg_order_value        float  — total_revenue / total_orders
 *   orders_placed          int    — status == "Order Placed"
 *   orders_out             int    — status == "Out for Delivery"
 *   orders_delivered       int    — status == "Delivered"
 *   orders_cancelled       int    — status == "Cancelled"
 *   slot_morning           int    — delivery_slot == "Morning"
 *   slot_afternoon         int    — delivery_slot == "Afternoon"
 *   slot_evening           int    — delivery_slot == "Evening"
 *   total_stock_kg         float  — sum of all stock_g / 1000
 *   low_stock_items        int    — count of vegs with stock_g < 5000
 *   total_users            int    — count of all registered users
 *   active_delivery_boys   int    — delivery boys with is_active == 1
 *   inactive_delivery_boys int    — delivery boys with is_active == 0
 *
 * Team: CodeCrafters | Project: Fresh Picks | SDP-1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "models.h"


/* HELPER STRUCTS — mirrors the three logical domains of the dashboard */

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


/* FUNCTION: calc_order_metrics
 * PURPOSE:  Walk the order table once, accumulate revenue and
 *           categorise each order by status and delivery slot.
 * PARAMS:   order_table      — O(1) pointer table built in main()
 *           order_table_size — size of the pointer table
 *           out              — caller-allocated OrderMetrics to fill
 */
static void calc_order_metrics(
    OrderNode **order_table,
    int order_table_size,
    OrderMetrics *out) {
    memset(out, 0, sizeof(OrderMetrics));

    for (int i = 0; i < order_table_size; i++) {
        if (!order_table[i]) continue;

        Order *o = &order_table[i]->data;
        out->total_revenue += o->total_amount;
        out->total_orders += 1;

        /* Status categorisation */
        if      (strcmp(o->status, "Order Placed")     == 0) out->orders_placed += 1;
        else if (strcmp(o->status, "Out for Delivery") == 0) out->orders_out += 1;
        else if (strcmp(o->status, "Delivered")        == 0) out->orders_delivered += 1;
        else if (strcmp(o->status, "Cancelled")        == 0) out->orders_cancelled += 1;

        /* Delivery slot categorisation*/
        if      (strcmp(o->delivery_slot, "Morning")   == 0) out->slot_morning += 1;
        else if (strcmp(o->delivery_slot, "Afternoon") == 0) out->slot_afternoon += 1;
        else if (strcmp(o->delivery_slot, "Evening")   == 0) out->slot_evening += 1;
    }
}


/* FUNCTION: calc_inventory_metrics
 * PURPOSE:  Walk the veg table once, sum stock in kg and count
 *           items below the low-stock threshold (5000 g).
 * PARAMS:   veg_table      — O(1) pointer table built in main()
 *           veg_table_size — size of the pointer table
 *           out            — caller-allocated InventoryMetrics to fill
 */
static void calc_inventory_metrics(
    VegNode **veg_table, 
    int veg_table_size,
    InventoryMetrics *out) {
    memset(out, 0, sizeof(InventoryMetrics));

    for (int i = 0; i < veg_table_size; i++) {
        if (!veg_table[i]) continue;

        int stock = veg_table[i]->data.stock_g;
        out->total_stock_kg += stock / 1000.0f;
        if (stock < 5000) out->low_stock_items += 1;
    }
}


/* FUNCTION: calc_staff_metrics
 * PURPOSE:  Count total users (linear SLL walk — no table needed) and
 *           categorise delivery boys by is_active via the pointer table.
 * PARAMS:   user_head      — user SLL head
 *           boy_table      — O(1) pointer table built in main()
 *           boy_table_size — size of the pointer table
 *           out            — caller-allocated StaffMetrics to fill
 */
static void calc_staff_metrics(
    UserNode *user_head,
    DeliveryBoyNode **boy_table, int boy_table_size,
    StaffMetrics *out) {
    memset(out, 0, sizeof(StaffMetrics));

    /* Count users via SLL — simple linear walk, no table overhead */
    UserNode *ucurr = user_head;
    while (ucurr != NULL) {
        out->total_users += 1;
        ucurr = ucurr->next;
    }

    /* Categorise delivery boys via O(1) table */
    for (int i = 0; i < boy_table_size; i++) {
        if (!boy_table[i]) continue;
        if (boy_table[i]->data.is_active == 1) out->active_delivery_boys += 1;
        else                                    out->inactive_delivery_boys += 1;
    }
}


/* FUNCTION: cmd_get_analytics
 * PURPOSE:  Receive all pre-built tables from main(), delegate to the
 *           three calc_* helpers, then emit SUCCESS + key|value lines.
 * PARAMS:   All pointer tables and their sizes, plus user SLL head for
 *           the linear user count.
 * OUTPUT:   SUCCESS\nkey|value\n...
 * SCHEMA:   See file-level comment for the full ordered key list.
 */
static void cmd_get_analytics(
    OrderNode **order_table,
    int order_table_size,
    VegNode   **veg_table,
    int veg_table_size,
    UserNode   *user_head,
    DeliveryBoyNode **boy_table, int boy_table_size) {
    OrderMetrics     om;
    InventoryMetrics im;
    StaffMetrics     sm;

    calc_order_metrics    (order_table, order_table_size, &om);
    calc_inventory_metrics(veg_table,   veg_table_size,   &im);
    calc_staff_metrics    (user_head, boy_table, boy_table_size, &sm);

    /* Average order value — guard divide-by-zero */
    float avg = (om.total_orders > 0)
                ? (om.total_revenue / (float)om.total_orders)
                : 0.0f;

    /* Emit output — SUCCESS header first, then key|value pairs */
    printf("SUCCESS\n");

    /* Revenue & order counts */
    printf("total_revenue|%.2f\n",om.total_revenue);
    printf("total_orders|%d\n",om.total_orders);
    printf("avg_order_value|%.2f\n",avg);

    /* Order status breakdown */
    printf("orders_placed|%d\n",om.orders_placed);
    printf("orders_out|%d\n",om.orders_out);
    printf("orders_delivered|%d\n",om.orders_delivered);
    printf("orders_cancelled|%d\n",om.orders_cancelled);

    /* Delivery slot breakdown */
    printf("slot_morning|%d\n",om.slot_morning);
    printf("slot_afternoon|%d\n",om.slot_afternoon);
    printf("slot_evening|%d\n",om.slot_evening);

    /* Inventory summary */
    printf("total_stock_kg|%.2f\n",im.total_stock_kg);
    printf("low_stock_items|%d\n",im.low_stock_items);

    /* User & staffing summary */
    printf("total_users|%d\n",sm.total_users);
    printf("active_delivery_boys|%d\n",sm.active_delivery_boys);
    printf("inactive_delivery_boys|%d\n",sm.inactive_delivery_boys);
}


/* MAIN — Load SLLs, build tables once, dispatch, free everything
 * PURPOSE:  Parse argv[1] and route to the appropriate cmd_* function.
 *           All SLL loading and table building happens here only.
 *           Guard clause on argc before any dispatch.*/
int main(int argc, char *argv[]) {
    if (argc < 2) {
        PRINT_ERROR("No command. Usage: ./analytics <command>");
        return 1;
    }

    /* Load all SLLs (source of truth)*/
    OrderNode       *order_head = load_order_sll();
    VegNode         *veg_head   = load_veg_sll();
    UserNode        *user_head  = load_user_sll();
    DeliveryBoyNode *boy_head   = load_delivery_boy_sll();

    /*Build pointer tables for O(1) access*/
    int order_table_size = 0;
    int veg_table_size   = 0;
    int boy_table_size   = DELIVERY_TABLE_SIZE;

    OrderNode       **order_table = build_order_table(order_head, &order_table_size);
    VegNode         **veg_table   = build_veg_table(veg_head,     &veg_table_size);

    /* Delivery-boy table — manually indexed using DELIVERY_TABLE_SIZE */
    DeliveryBoyNode **boy_table   = 
    (DeliveryBoyNode **)calloc(boy_table_size, sizeof(DeliveryBoyNode *));
    if (boy_table) {
        DeliveryBoyNode *bcurr = boy_head;
        while (bcurr != NULL) {
            int idx = get_index_from_id(bcurr->data.boy_id);
            if (idx >= 0 && idx < boy_table_size)
                boy_table[idx] = bcurr;
            bcurr = bcurr->next;
        }
    }

    if (!order_table) { PRINT_ERROR("Failed to build order table");    goto cleanup; }
    if (!veg_table)   { PRINT_ERROR("Failed to build veg table");      goto cleanup; }
    if (!boy_table)   { PRINT_ERROR("Failed to build delivery table"); goto cleanup; }

    const char *cmd = argv[1];

    if (strcmp(cmd, "get_analytics") == 0) {
        /* argv: analytics get_analytics
         * idx:  [0]       [1]
         * argc >= 2 (no additional args needed) */
        cmd_get_analytics(order_table, order_table_size,
                          veg_table,   veg_table_size,
                          user_head,
                          boy_table,   boy_table_size);

    } else {
        char err[MAX_STR_LEN];
        snprintf(err, sizeof(err), "Unknown command: %s", cmd);
        PRINT_ERROR(err);
    }

cleanup:
    /*Free tables first, then SLLs — strict memory safety*/
    free(order_table);
    free(veg_table);
    free(boy_table);
    free_order_sll(order_head);
    free_veg_sll(veg_head);
    free_user_sll(user_head);
    free_delivery_boy_sll(boy_head);

    return 0;
}
