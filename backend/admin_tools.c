/*
 * admin_tools.c - Fresh Picks: Admin Terminal Utility 
 *
 * Compile Command:  gcc -Wall -Wextra -o admin_tools admin_tools.c utils.c -lm
 * gcc -Wall -Wextra -o auth1 auth1.c utils.c -lm
 * Run Command:  .\admin_tools.exe .\auth1.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "models.h"


/* ═════════════════════════════════════════════════════════════
   SECTION 1: INPUT HELPERS
   ═════════════════════════════════════════════════════════════ */

static void flush_stdin(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

static void read_line(const char* prompt, char* buf, int size) {
    while (1) {
        printf("  %s", prompt);
        fflush(stdout);

        if (!fgets(buf, size, stdin)) {
            buf[0] = '\0';
            return;
        }

        buf[strcspn(buf, "\n")] = '\0';

        if (strlen(buf) > 0) return;

        printf("  [!] Input cannot be empty. Please try again.\n");
    }
}


/* ═════════════════════════════════════════════════════════════
   SECTION 2: SLL APPEND HELPERS
   ═════════════════════════════════════════════════════════════ */

static AdminNode* admin_sll_append(AdminNode* head, AdminNode* new_node) {
    new_node->next = NULL;
    if (!head) return new_node;
    AdminNode* tail = head;
    while (tail->next) tail = tail->next;
    tail->next = new_node;
    return head;
}

static DeliveryBoyNode* boy_sll_append(DeliveryBoyNode* head,
                                       DeliveryBoyNode* new_node) {
    new_node->next = NULL;
    if (!head) return new_node;
    DeliveryBoyNode* tail = head;
    while (tail->next) tail = tail->next;
    tail->next = new_node;
    return head;
}


/* ═════════════════════════════════════════════════════════════
   SECTION 3: COUNT HELPERS
   ═════════════════════════════════════════════════════════════ */

static int count_admins(AdminNode* head) {
    int count = 0;
    AdminNode* curr = head;
    while (curr) { count++; curr = curr->next; }
    return count;
}

static int count_delivery_boys(DeliveryBoyNode* head) {
    int count = 0;
    DeliveryBoyNode* curr = head;
    while (curr) { count++; curr = curr->next; }
    return count;
}


/* ═════════════════════════════════════════════════════════════
   SECTION 4: DISPLAY HELPERS
   ═════════════════════════════════════════════════════════════ */

static void print_admin_table(AdminNode* head) {
    printf("\n  %-10s %-20s %-25s %-20s\n",
           "ID", "Username", "Admin Name", "Email");
    printf("  %s\n",
           "----------------------------------------------------------------------");

    if (!head) {
        printf("  (no admin accounts found)\n");
        return;
    }

    AdminNode* curr = head;
    while (curr) {
        printf("  %-10s %-20s %-25s %-20s\n",
               curr->data.admin_id,
               curr->data.username,
               curr->data.admin_name,
               curr->data.email);
        curr = curr->next;
    }
}

static void print_boy_table(DeliveryBoyNode* head) {
    printf("\n  %-10s %-15s %-15s %-18s %-9s %-13s\n",
           "ID", "Name", "Phone", "Vehicle No", "Active", "Last Assigned");
    printf("  %s\n",
           "----------------------------------------------------------------------------");

    if (!head) {
        printf("  (no delivery boys found)\n");
        return;
    }

    DeliveryBoyNode* curr = head;
    while (curr) {
        printf("  %-10s %-15s %-15s %-18s %-9s %-13s\n",
               curr->data.boy_id,
               curr->data.name,
               curr->data.phone,
               curr->data.vehicle_no,
               curr->data.is_active     ? "Yes" : "No",
               curr->data.last_assigned ? "Yes" : "No");
        curr = curr->next;
    }
}


/* ═════════════════════════════════════════════════════════════
   SECTION 5: COMMAND FUNCTIONS
   ═════════════════════════════════════════════════════════════ */

static void cmd_add_admin(void) {
    printf("\n╔══════════════════════════════════╗\n");
    printf(  "║        ADD NEW ADMIN ACCOUNT     ║\n");
    printf(  "╚══════════════════════════════════╝\n");

    AdminNode* head = load_admin_sll();

    printf("\n  Existing admins:\n");
    print_admin_table(head);
    printf("\n");

    int  count    = count_admins(head);
    int  next_num = 1001 + count;
    char new_id[MAX_ID_LEN];
    snprintf(new_id, MAX_ID_LEN, "A%d", next_num);
    printf("  New Admin ID will be: %s\n\n", new_id);

    char username  [MAX_STR_LEN];
    char password  [MAX_STR_LEN];
    char admin_name[MAX_STR_LEN];
    char email     [MAX_STR_LEN];

    read_line("Username    : ", username, MAX_STR_LEN);

    AdminNode* curr = head;
    while (curr) {
        if (strcmp(curr->data.username, username) == 0) {
            printf("\n  [ERROR] Username \"%s\" is already taken. "
                   "Returning to menu.\n", username);
            free_admin_sll(head);
            return;
        }
        curr = curr->next;
    }

    read_line("Password    : ", password,   MAX_STR_LEN);
    read_line("Admin Name  : ", admin_name, MAX_STR_LEN);
    read_line("Email       : ", email,      MAX_STR_LEN);

    AdminNode* new_node = (AdminNode*)malloc(sizeof(AdminNode));
    if (!new_node) {
        printf("\n  [ERROR] Memory allocation failed.\n");
        free_admin_sll(head);
        return;
    }

    memset(&new_node->data, 0, sizeof(AdminCreds));
    strncpy(new_node->data.admin_id,   new_id,     MAX_ID_LEN  - 1);
    strncpy(new_node->data.username,   username,   MAX_STR_LEN - 1);
    strncpy(new_node->data.password,   password,   MAX_STR_LEN - 1);
    strncpy(new_node->data.admin_name, admin_name, MAX_STR_LEN - 1);
    strncpy(new_node->data.email,      email,      MAX_STR_LEN - 1);
    new_node->next = NULL;

    head = admin_sll_append(head, new_node);
    save_admin_sll(head);
    free_admin_sll(head);

    printf("\n  [OK] Admin \"%s\" (%s) added successfully.\n", username, new_id);
}

static void cmd_add_delivery_boy(void) {
    printf("\n╔══════════════════════════════════╗\n");
    printf(  "║       ADD NEW DELIVERY BOY       ║\n");
    printf(  "╚══════════════════════════════════╝\n");

    DeliveryBoyNode* head = load_delivery_boy_sll();

    printf("\n  Existing delivery boys:\n");
    print_boy_table(head);
    printf("\n");

    int  count    = count_delivery_boys(head);
    int  next_num = 1001 + count;
    char new_id[MAX_ID_LEN];
    snprintf(new_id, MAX_ID_LEN, "D%d", next_num);
    printf("  New Delivery Boy ID will be: %s\n\n", new_id);

    char name      [MAX_STR_LEN];
    char phone     [MAX_STR_LEN];
    char vehicle_no[MAX_STR_LEN];
    char active_buf[8];

    read_line("Name          : ", name,       MAX_STR_LEN);
    read_line("Phone         : ", phone,      MAX_STR_LEN);
    read_line("Vehicle No    : ", vehicle_no, MAX_STR_LEN);

    int is_active = 1;
    while (1) {
        read_line("Is Active? [1=Yes / 0=No] (default 1): ",
                  active_buf, sizeof(active_buf));
        if (strlen(active_buf) == 0 || strcmp(active_buf, "1") == 0) {
            is_active = 1; break;
        } else if (strcmp(active_buf, "0") == 0) {
            is_active = 0; break;
        }
        printf("  [!] Please enter 1 or 0.\n");
    }

    DeliveryBoyNode* curr = head;
    while (curr) {
        curr->data.last_assigned = 0;
        curr = curr->next;
    }

    DeliveryBoyNode* new_node = (DeliveryBoyNode*)malloc(sizeof(DeliveryBoyNode));
    if (!new_node) {
        printf("\n  [ERROR] Memory allocation failed.\n");
        free_delivery_boy_sll(head);
        return;
    }

    memset(&new_node->data, 0, sizeof(DeliveryBoy));
    strncpy(new_node->data.boy_id,     new_id,     MAX_ID_LEN  - 1);
    strncpy(new_node->data.name,       name,       MAX_STR_LEN - 1);
    strncpy(new_node->data.phone,      phone,      MAX_STR_LEN - 1);
    strncpy(new_node->data.vehicle_no, vehicle_no, MAX_STR_LEN - 1);
    new_node->data.is_active     = is_active;
    new_node->data.last_assigned = 1;
    new_node->next = NULL;

    head = boy_sll_append(head, new_node);
    save_delivery_boy_sll(head);
    free_delivery_boy_sll(head);

    printf("\n  [OK] Delivery Boy \"%s\" (%s) added successfully.\n", name, new_id);
    printf("  [INFO] All existing boys' last_assigned reset to 0.\n");
    printf("  [INFO] CLL will restart from the first boy on next order.\n");
}

static void cmd_view_admins(void) {
    AdminNode* head = load_admin_sll();
    print_admin_table(head);
    free_admin_sll(head);
}

static void cmd_view_delivery_boys(void) {
    DeliveryBoyNode* head = load_delivery_boy_sll();
    print_boy_table(head);
    free_delivery_boy_sll(head);
}

/* cmd_update_admin — O(1) lookup by admin_id, update one field, persist */
static void cmd_update_admin(const char *admin_id, const char *field,
                             const char *new_value,
                             AdminNode **admin_table, AdminNode *admin_head) {
    int valid_field = (strcmp(field, "username")   == 0 ||
                       strcmp(field, "password")   == 0 ||
                       strcmp(field, "admin_name") == 0 ||
                       strcmp(field, "email")      == 0);
    if (!valid_field) {
        printf("\n  [ERROR] Unknown field. Valid: username, password, admin_name, email\n");
        return;
    }

    int idx = get_index_from_id(admin_id);
    if (idx < 0 || idx >= ADMIN_TABLE_SIZE || !admin_table[idx]) {
        printf("\n  [ERROR] Admin not found.\n");
        return;
    }

    AdminNode *match = admin_table[idx];

    if      (strcmp(field, "username")   == 0) strncpy(match->data.username,   new_value, MAX_STR_LEN - 1);
    else if (strcmp(field, "password")   == 0) strncpy(match->data.password,   new_value, MAX_STR_LEN - 1);
    else if (strcmp(field, "admin_name") == 0) strncpy(match->data.admin_name, new_value, MAX_STR_LEN - 1);
    else                                       strncpy(match->data.email,      new_value, MAX_STR_LEN - 1);

    match->data.username  [MAX_STR_LEN - 1] = '\0';
    match->data.password  [MAX_STR_LEN - 1] = '\0';
    match->data.admin_name[MAX_STR_LEN - 1] = '\0';
    match->data.email     [MAX_STR_LEN - 1] = '\0';

    save_admin_sll(admin_head);
    printf("\n  [OK] Admin updated successfully.\n");
}

/* cmd_edit_admin — interactive wrapper around cmd_update_admin */
static void cmd_edit_admin(void) {
    AdminNode  *admin_head  = load_admin_sll();
    AdminNode **admin_table = build_admin_table(admin_head);

    if (!admin_table) {
        printf("\n  [!] Failed to load admin table.\n");
        free_admin_sll(admin_head);
        return;
    }

    printf("\n╔══════════════════════════════════╗\n");
    printf(  "║           EDIT ADMIN             ║\n");
    printf(  "╚══════════════════════════════════╝\n");

    printf("\n  Existing admins:\n");
    print_admin_table(admin_head);
    printf("\n");

    char admin_id [MAX_ID_LEN];
    char field    [MAX_STR_LEN];
    char new_value[MAX_STR_LEN];

    read_line("Admin ID                                    : ", admin_id,  MAX_ID_LEN);
    printf("  Field    : (username / password / admin_name / email)\n");
    read_line("> ", field,     MAX_STR_LEN);
    read_line("New Value                                   : ", new_value, MAX_STR_LEN);

    cmd_update_admin(admin_id, field, new_value, admin_table, admin_head);

    free(admin_table);
    free_admin_sll(admin_head);
}


/* ═════════════════════════════════════════════════════════════
   SECTION 6: MAIN — Interactive Menu
   ═════════════════════════════════════════════════════════════ */

int main(void) {
    system("chcp 65001");

    printf("\n");
    printf("  ███████╗██████╗ ███████╗███████╗██╗  ██╗\n");
    printf("  ██╔════╝██╔══██╗██╔════╝██╔════╝██║  ██║\n");
    printf("  █████╗  ██████╔╝█████╗  ███████╗███████║\n");
    printf("  ██╔══╝  ██╔══██╗██╔══╝  ╚════██║██╔══██║\n");
    printf("  ██║     ██║  ██║███████╗███████║██║  ██║\n");
    printf("  ╚═╝     ╚═╝  ╚═╝╚══════╝╚══════╝╚═╝  ╚═╝\n");
    printf("         PICKS  —  Admin Tools  (v4)\n");
    printf("  ════════════════════════════════════════\n\n");

    char choice_buf[8];
    int running = 1;

    while (running) {
        printf("\n  ┌─────────────────────────────┐\n");
        printf(  "  │         MAIN  MENU          │\n");
        printf(  "  ├─────────────────────────────┤\n");
        printf(  "  │  [1]  Add Admin             │\n");
        printf(  "  │  [2]  Add Delivery Boy      │\n");
        printf(  "  │  [3]  View Admins           │\n");
        printf(  "  │  [4]  View Delivery Boys    │\n");
        printf(  "  │  [5]  Edit Admin            │\n");
        printf(  "  │  [6]  Exit                  │\n");
        printf(  "  └─────────────────────────────┘\n");
        printf(  "  Choice: ");
        fflush(stdout);

        if (!fgets(choice_buf, sizeof(choice_buf), stdin)) break;
        choice_buf[strcspn(choice_buf, "\n")] = '\0';

        if (strlen(choice_buf) == sizeof(choice_buf) - 1)
            flush_stdin();

        int choice = atoi(choice_buf);

        switch (choice) {
            case 1: cmd_add_admin();          break;
            case 2: cmd_add_delivery_boy();   break;
            case 3: cmd_view_admins();        break;
            case 4: cmd_view_delivery_boys(); break;
            case 5: cmd_edit_admin();         break;
            case 6:
                printf("\n  Goodbye.\n\n");
                running = 0;
                break;
            default:
                printf("\n  [!] Invalid choice. Enter 1–6.\n");
                break;
        }
    }

    return 0;
}