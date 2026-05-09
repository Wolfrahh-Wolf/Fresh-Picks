/*
 * analytics.c - Fresh Picks: Admin Analytics Dashboard
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "models.h"


/* ═════════════════════════════════════════════════════════════
   SECTION 1: METRIC CALCULATOR FUNCTIONS
   ═════════════════════════════════════════════════════════════ */

/* Walk the order SLL once; accumulate revenue and categorise by status and slot. */
OrderMetrics calc_order_metrics(OrderNode *head) {
    OrderMetrics orderMet;
    memset(&orderMet, 0, sizeof(OrderMetrics));

    OrderNode *curr = head;
    while (curr != NULL) {
        Order *ord = &curr->data;
        orderMet.total_revenue += ord->total_amount;
        orderMet.total_orders  += 1;

        /* Status categorisation */
        if      (strcmp(ord->status, "Order Placed")     == 0) orderMet.orders_placed    += 1;
        else if (strcmp(ord->status, "Out for Delivery") == 0) orderMet.orders_out       += 1;
        else if (strcmp(ord->status, "Delivered")        == 0) orderMet.orders_delivered += 1;
        else if (strcmp(ord->status, "Cancelled")        == 0) orderMet.orders_cancelled += 1;

        /* Delivery slot categorisation */
        if      (strcmp(ord->delivery_slot, "Morning")   == 0) orderMet.slot_morning   += 1;
        else if (strcmp(ord->delivery_slot, "Afternoon") == 0) orderMet.slot_afternoon += 1;
        else if (strcmp(ord->delivery_slot, "Evening")   == 0) orderMet.slot_evening   += 1;

        curr = curr->next;
    }

    return orderMet;
}

/* Walk the veg SLL once; sum stock in kg and count items below 5000g threshold. */
InventoryMetrics calc_inventory_metrics(VegNode *head) {
    InventoryMetrics inventMet;
    memset(&inventMet, 0, sizeof(InventoryMetrics));

    VegNode *curr = head;
    while (curr != NULL) {
        int stock = curr->data.stock_g;
        inventMet.total_stock_kg += stock / 1000.0f;
        if (stock < 5000) inventMet.low_stock_items += 1;
        curr = curr->next;
    }

    return inventMet;
}

/* Walk both SLLs; count users and categorise delivery boys by is_active. */
StaffMetrics calc_staff_metrics(UserNode *u_head, DeliveryBoyNode *b_head) {
    StaffMetrics staffMet;
    memset(&staffMet, 0, sizeof(StaffMetrics));

    UserNode *ucurr = u_head;
    while (ucurr != NULL) {
        staffMet.total_users += 1;
        ucurr = ucurr->next;
    }

    DeliveryBoyNode *bcurr = b_head;
    while (bcurr != NULL) {
        if (bcurr->data.is_active == 1) staffMet.active_delivery_boys   += 1;
        else                            staffMet.inactive_delivery_boys += 1;
        bcurr = bcurr->next;
    }

    return staffMet;
}


/* ═════════════════════════════════════════════════════════════
   SECTION 2: COMMAND HANDLER
   ═════════════════════════════════════════════════════════════ */

/*
 * Call the three calc functions, then emit SUCCESS + key|value lines.
 * Returns 0 on success.
 */
int cmd_get_analytics(OrderNode *ord_head, VegNode *veg_head,
                      UserNode *user_head, DeliveryBoyNode *boy_head) {
    OrderMetrics     orderMet = calc_order_metrics(ord_head);
    InventoryMetrics inventMet = calc_inventory_metrics(veg_head);
    StaffMetrics     staffMet = calc_staff_metrics(user_head, boy_head);

    /* Average order value — guard divide-by-zero */
    float avg = (orderMet.total_orders > 0)
                ? (orderMet.total_revenue / (float)orderMet.total_orders)
                : 0.0f;

    printf("SUCCESS|\n");

    /* Revenue & order counts */
    printf("total_revenue|%.2f\n",        orderMet.total_revenue);
    printf("total_orders|%d\n",           orderMet.total_orders);
    printf("avg_order_value|%.2f\n",      avg);

    /* Order status breakdown */
    printf("orders_placed|%d\n",          orderMet.orders_placed);
    printf("orders_out|%d\n",             orderMet.orders_out);
    printf("orders_delivered|%d\n",       orderMet.orders_delivered);
    printf("orders_cancelled|%d\n",       orderMet.orders_cancelled);

    /* Delivery slot breakdown */
    printf("slot_morning|%d\n",           orderMet.slot_morning);
    printf("slot_afternoon|%d\n",         orderMet.slot_afternoon);
    printf("slot_evening|%d\n",           orderMet.slot_evening);

    /* Inventory summary */
    printf("total_stock_kg|%.2f\n",       inventMet.total_stock_kg);
    printf("low_stock_items|%d\n",        inventMet.low_stock_items);

    /* User & staffing summary */
    printf("total_users|%d\n",            staffMet.total_users);
    printf("active_delivery_boys|%d\n",   staffMet.active_delivery_boys);
    printf("inactive_delivery_boys|%d\n", staffMet.inactive_delivery_boys);

    return 0;
}


/* ═════════════════════════════════════════════════════════════
   SECTION 3: MAIN — Load, Dispatch, Free
   ═════════════════════════════════════════════════════════════ */

/*
 * Flask calls this binary via:
 *   subprocess.run(["./analytics", "get_analytics"], ...)
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        PRINT_ERROR("No command provided. Usage: ./analytics <command>");
        return 1;
    }

    const char *cmd = argv[1];

    /* Load all SLLs */
    OrderNode       *ord_head  = load_order_sll();
    VegNode         *veg_head  = load_veg_sll();
    UserNode        *user_head = load_user_sll();
    DeliveryBoyNode *boy_head  = load_delivery_boy_sll();

    /* Dispatch */
    if (strcmp(cmd, "get_analytics") == 0) {
        cmd_get_analytics(ord_head, veg_head, user_head, boy_head);

    } else {
        char err[MAX_STR_LEN];
        snprintf(err, sizeof(err), "Unknown command: %s", cmd);
        PRINT_ERROR(err);
    }

    /* Free all SLLs */
    free_order_sll(ord_head);
    free_veg_sll(veg_head);
    free_user_sll(user_head);
    free_delivery_boy_sll(boy_head);

    return 0;
}
