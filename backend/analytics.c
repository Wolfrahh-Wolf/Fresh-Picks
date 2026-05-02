/*
 * analytics.c - Fresh Picks: Admin Analytics Dashboard (v1 — Binary Storage)
 * =============================================================================
 * This binary aggregates metrics across all .dat files and prints them
 * in a machine-readable key|value format for Flask to consume.
 * Called by Flask via subprocess.run() like this:
 *   ./analytics <command>
 *
 * ALL persistent I/O is delegated to utils.c (load/free SLL functions).
 * Direct fopen / fgets / strtok / fprintf on data files is STRICTLY FORBIDDEN.
 *
 * OUTPUT CONTRACT:
 *   First line:  SUCCESS
 *   Every subsequent line: key|value  (no headers, no blank lines)
 *   On failure:  ERROR|reason
 *
 * COMMANDS (argv[1]):
 *   get_analytics      — Compute and print all dashboard metrics
 *
 * METRICS EMITTED (in order):
 *   total_revenue        — Sum of total_amount across all orders
 *   total_orders         — Count of all orders
 *   avg_order_value      — total_revenue / total_orders (0.00 if no orders)
 *   orders_placed        — Count with status "Order Placed"
 *   orders_out           — Count with status "Out for Delivery"
 *   orders_delivered     — Count with status "Delivered"
 *   orders_cancelled     — Count with status "Cancelled"
 *   slot_morning         — Count with delivery_slot "Morning"
 *   slot_afternoon       — Count with delivery_slot "Afternoon"
 *   slot_evening         — Count with delivery_slot "Evening"
 *   total_stock_kg       — Sum of all veg stock_g converted to kg
 *   low_stock_items      — Count of vegetables with stock_g < 5000
 *   total_users          — Count of all registered users
 *   active_delivery_boys — Count of delivery boys with is_active == 1
 *   inactive_delivery_boys — Count of delivery boys with is_active == 0
 *
 * Team: CodeCrafters | Project: Fresh Picks | SDP-1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "models.h"   /* Struct definitions, SLL node types, macros, utils.c API */


/* ══════════════════════════════════════════════════════════════════════
 * STRUCT: OrderMetrics
 * PURPOSE: Holds all computed order-related counters and sums so
 *          cmd_get_analytics() can pass them around cleanly.
 * ══════════════════════════════════════════════════════════════════════ */
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

/* ══════════════════════════════════════════════════════════════════════
 * STRUCT: InventoryMetrics
 * PURPOSE: Holds all computed inventory-related totals.
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
    float total_stock_kg;
    int   low_stock_items;
} InventoryMetrics;

/* ══════════════════════════════════════════════════════════════════════
 * STRUCT: StaffMetrics
 * PURPOSE: Holds user count and delivery-boy breakdown.
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
    int total_users;
    int active_delivery_boys;
    int inactive_delivery_boys;
} StaffMetrics;


/* ══════════════════════════════════════════════════════════════════════
 * HELPER: calc_order_metrics
 * PURPOSE: Walk the order SLL once and populate an OrderMetrics struct.
 *          Uses strcmp() on status and delivery_slot — exact same
 *          pattern as cmd_update_stock()'s SLL traversal.
 * PARAMS:  head — head of the loaded OrderNode SLL
 *          out  — pointer to caller-allocated OrderMetrics to fill
 * ══════════════════════════════════════════════════════════════════════ */
static void calc_order_metrics(OrderNode *head, OrderMetrics *out) {
    memset(out, 0, sizeof(OrderMetrics));

    OrderNode *curr = head;
    while (curr != NULL) {
        out->total_revenue += curr->data.total_amount;
        out->total_orders  += 1;

        /* ── Status categorisation ── */
        if      (strcmp(curr->data.status, "Order Placed")      == 0) out->orders_placed    += 1;
        else if (strcmp(curr->data.status, "Out for Delivery")   == 0) out->orders_out       += 1;
        else if (strcmp(curr->data.status, "Delivered")          == 0) out->orders_delivered += 1;
        else if (strcmp(curr->data.status, "Cancelled")          == 0) out->orders_cancelled += 1;

        /* ── Delivery slot categorisation ── */
        if      (strcmp(curr->data.delivery_slot, "Morning")   == 0) out->slot_morning   += 1;
        else if (strcmp(curr->data.delivery_slot, "Afternoon") == 0) out->slot_afternoon += 1;
        else if (strcmp(curr->data.delivery_slot, "Evening")   == 0) out->slot_evening   += 1;

        curr = curr->next;
    }
}


/* ══════════════════════════════════════════════════════════════════════
 * HELPER: calc_inventory_metrics
 * PURPOSE: Walk the vegetable SLL once, accumulate total stock in kg
 *          and count items below the low-stock threshold (5000 g).
 * PARAMS:  head — head of the loaded VegNode SLL
 *          out  — pointer to caller-allocated InventoryMetrics to fill
 * ══════════════════════════════════════════════════════════════════════ */
static void calc_inventory_metrics(VegNode *head, InventoryMetrics *out) {
    memset(out, 0, sizeof(InventoryMetrics));

    VegNode *curr = head;
    while (curr != NULL) {
        out->total_stock_kg += curr->data.stock_g / 1000.0f;
        if (curr->data.stock_g < 5000) {
            out->low_stock_items += 1;
        }
        curr = curr->next;
    }
}


/* ══════════════════════════════════════════════════════════════════════
 * HELPER: calc_staff_metrics
 * PURPOSE: Count total users and walk the delivery-boy SLL to tally
 *          active vs inactive agents.
 * PARAMS:  user_head — head of the loaded UserNode SLL
 *          boy_head  — head of the loaded DeliveryBoyNode SLL
 *          out       — pointer to caller-allocated StaffMetrics to fill
 * ══════════════════════════════════════════════════════════════════════ */
static void calc_staff_metrics(UserNode *user_head, DeliveryBoyNode *boy_head,
                                StaffMetrics *out) {
    memset(out, 0, sizeof(StaffMetrics));

    /* Count users */
    UserNode *ucurr = user_head;
    while (ucurr != NULL) {
        out->total_users += 1;
        ucurr = ucurr->next;
    }

    /* Categorise delivery boys by is_active flag */
    DeliveryBoyNode *bcurr = boy_head;
    while (bcurr != NULL) {
        if (bcurr->data.is_active == 1) out->active_delivery_boys   += 1;
        else                            out->inactive_delivery_boys  += 1;
        bcurr = bcurr->next;
    }
}


/* ══════════════════════════════════════════════════════════════════════
 * FUNCTION: cmd_get_analytics
 * PURPOSE:  Load all four SLLs, delegate computation to the three
 *           calc_* helpers, print SUCCESS followed by key|value lines,
 *           then free every SLL before returning.
 * PARAMS:   (none)
 * OUTPUT:   SUCCESS\nkey|value\n...   OR   ERROR|reason
 * SCHEMA:   See file-level comment for the full ordered key list.
 * ══════════════════════════════════════════════════════════════════════ */
void cmd_get_analytics(void) {

    /* ── Load all SLLs ── */
    OrderNode       *order_head = load_order_sll();
    VegNode         *veg_head   = load_veg_sll();
    UserNode        *user_head  = load_user_sll();
    DeliveryBoyNode *boy_head   = load_delivery_boy_sll();

    /* Guard: at least orders and products must exist for a useful report.
     * Users / delivery-boy files may be empty on a fresh install — we
     * treat those as zero counts rather than hard errors. */
    if (!order_head) {
        free_veg_sll(veg_head);
        free_user_sll(user_head);
        free_delivery_boy_sll(boy_head);
        PRINT_ERROR("No order data found");
        return;
    }
    if (!veg_head) {
        free_order_sll(order_head);
        free_user_sll(user_head);
        free_delivery_boy_sll(boy_head);
        PRINT_ERROR("No product data found");
        return;
    }

    /* ── Compute metrics via helpers ── */
    OrderMetrics     om;
    InventoryMetrics im;
    StaffMetrics     sm;

    calc_order_metrics    (order_head, &om);
    calc_inventory_metrics(veg_head,   &im);
    calc_staff_metrics    (user_head, boy_head, &sm);

    /* ── Average order value (guard against divide-by-zero) ── */
    float avg_order_value = (om.total_orders > 0)
                            ? (om.total_revenue / (float)om.total_orders)
                            : 0.0f;

    /* ── Emit output — SUCCESS header first, then key|value pairs ── */
    printf("SUCCESS\n");

    /* Revenue & order counts */
    printf("total_revenue|%.2f\n",    om.total_revenue);
    printf("total_orders|%d\n",       om.total_orders);
    printf("avg_order_value|%.2f\n",  avg_order_value);

    /* Order status breakdown */
    printf("orders_placed|%d\n",      om.orders_placed);
    printf("orders_out|%d\n",         om.orders_out);
    printf("orders_delivered|%d\n",   om.orders_delivered);
    printf("orders_cancelled|%d\n",   om.orders_cancelled);

    /* Delivery slot breakdown */
    printf("slot_morning|%d\n",       om.slot_morning);
    printf("slot_afternoon|%d\n",     om.slot_afternoon);
    printf("slot_evening|%d\n",       om.slot_evening);

    /* Inventory summary */
    printf("total_stock_kg|%.2f\n",   im.total_stock_kg);
    printf("low_stock_items|%d\n",    im.low_stock_items);

    /* User & staffing summary */
    printf("total_users|%d\n",        sm.total_users);
    printf("active_delivery_boys|%d\n",   sm.active_delivery_boys);
    printf("inactive_delivery_boys|%d\n", sm.inactive_delivery_boys);

    /* ── Free all SLLs — strict memory safety ── */
    free_order_sll(order_head);
    free_veg_sll(veg_head);
    free_user_sll(user_head);
    free_delivery_boy_sll(boy_head);
}


/* ══════════════════════════════════════════════════════════════════════
 * MAIN — Command Dispatcher
 * PURPOSE:  Parse argv[1] and route to the appropriate cmd_* function.
 *           Guard clause on argc before any dispatch.
 * ══════════════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        PRINT_ERROR("No command. Usage: ./analytics <command>");
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "get_analytics") == 0) {
        /* argv: analytics get_analytics
         * idx:  [0]       [1]
         * argc >= 2 (no additional args needed) */
        cmd_get_analytics();

    } else {
        char err[MAX_STR_LEN];
        snprintf(err, sizeof(err), "Unknown command: %s", cmd);
        PRINT_ERROR(err);
        return 1;
    }

    return 0;
}
