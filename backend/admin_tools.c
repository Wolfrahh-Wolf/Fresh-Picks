/*
 * admin_tools.c - Fresh Picks: Admin Terminal Utility
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "models.h"


/* ═════════════════════════════════════════════════════════════
   SECTION 1: INPUT HELPERS
   ═════════════════════════════════════════════════════════════ */

// Discard any leftover characters in the stdin buffer
// (including the trailing newline left by scanf/fgets). 
// Must be called before every fgets() to avoid phantom reads.
static void flush_stdin(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/*
 * FUNCTION: read_line
 * PURPOSE:  Read one non-empty line of user input into buf.
 *           Strips the trailing newline. Re-prompts if the user
 *           presses Enter without typing anything.
 * PARAMS:   prompt — the label to print before the input cursor
 *           buf    — destination buffer
 *           size   — sizeof(buf) — bounds the read
 * OUTPUT:   (none — fills buf)
 * SCHEMA:   (none)
 */
static void read_line(const char* prompt, char* buf, int size) {
    while (1) {
        printf("  %s", prompt);
        fflush(stdout);

        if (!fgets(buf, size, stdin)) {
            buf[0] = '\0';
            return;
        }

        /* Strip trailing newline */
        buf[strcspn(buf, "\n")] = '\0';

        if (strlen(buf) > 0) return;   /* Got something — done */

        printf("  [!] Input cannot be empty. Please try again.\n");
    }
}


/* ═════════════════════════════════════════════════════════════
   SECTION 2: SLL APPEND HELPERS
   Generic tail-append used by both add functions.
   ═════════════════════════════════════════════════════════════ */

/*
 * FUNCTION: admin_sll_append
 * PURPOSE:  Append a new AdminNode at the tail of an existing SLL.
 *           If head is NULL (empty list), the new node becomes the head.
 * PARAMS:   head     — current SLL head (may be NULL)
 *           new_node — the node to append
 * OUTPUT:   (none — returns new head of SLL)
 * SCHEMA:   (none)
 */
static AdminNode* admin_sll_append(AdminNode* head, AdminNode* new_node) {
    new_node->next = NULL;

    if (!head) return new_node;

    AdminNode* tail = head;
    while (tail->next) tail = tail->next;
    tail->next = new_node;
    return head;
}

/*
 * FUNCTION: boy_sll_append
 * PURPOSE:  Append a new DeliveryBoyNode at the tail of an existing SLL.
 *           If head is NULL (empty list), the new node becomes the head.
 * PARAMS:   head     — current SLL head (may be NULL)
 *           new_node — the node to append
 * OUTPUT:   (none — returns new head of SLL)
 * SCHEMA:   (none)
 */
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
   utils.c exposes sll_count_orders and sll_count_users but not
   admin or delivery boy counts — so we count locally.
   ═════════════════════════════════════════════════════════════ */

/*
 * FUNCTION: count_admins
 * PURPOSE:  Walk the AdminNode SLL and return the number of nodes.
 * PARAMS:   head — head of the loaded AdminNode SLL (may be NULL)
 * OUTPUT:   (none — returns int count)
 * SCHEMA:   (none)
 */
static int count_admins(AdminNode* head) {
    int count = 0;
    AdminNode* curr = head;
    while (curr) { count++; curr = curr->next; }
    return count;
}

/*
 * FUNCTION: count_delivery_boys
 * PURPOSE:  Walk the DeliveryBoyNode SLL and return the number of nodes.
 * PARAMS:   head — head of the loaded DeliveryBoyNode SLL (may be NULL)
 * OUTPUT:   (none — returns int count)
 * SCHEMA:   (none)
 */
static int count_delivery_boys(DeliveryBoyNode* head) {
    int count = 0;
    DeliveryBoyNode* curr = head;
    while (curr) { count++; curr = curr->next; }
    return count;
}


/* ═════════════════════════════════════════════════════════════
   SECTION 4: DISPLAY HELPERS
   Print the current state of each SLL in a readable table.
   ═════════════════════════════════════════════════════════════ */

/*
 * FUNCTION: print_admin_table
 * PURPOSE:  Print all existing Admin accounts as a formatted table.
 *           Called before the "Add Admin" prompt so the operator
 *           can see what already exists.
 * PARAMS:   head — head of the loaded AdminNode SLL (may be NULL)
 * OUTPUT:   (none — prints to stdout)
 * SCHEMA:   (none)
 */
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

/*
 * FUNCTION: print_boy_table
 * PURPOSE:  Print all existing Delivery Boys as a formatted table.
 *           Called before the "Add Delivery Boy" prompt so the operator
 *           can see what already exists.
 * PARAMS:   head — head of the loaded DeliveryBoyNode SLL (may be NULL)
 * OUTPUT:   (none — prints to stdout)
 * SCHEMA:   (none)
 */
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
               curr->data.is_active   ? "Yes" : "No",
               curr->data.last_assigned ? "Yes" : "No");
        curr = curr->next;
    }
}


/* ═════════════════════════════════════════════════════════════
   SECTION 5: COMMAND FUNCTIONS
   ═════════════════════════════════════════════════════════════ */

/*
 * FUNCTION: cmd_add_admin
 * PURPOSE:  Interactively add a new Admin account to admin_creds.dat.
 *
 * PIPELINE:
 *   1. Load the existing Admin SLL via load_admin_sll().
 *   2. Display current admins so the operator has context.
 *   3. Calculate the next ID: A(1001 + count).
 *   4. Prompt for: username, password, admin_name, email.
 *   5. Duplicate username check — reject if already taken.
 *   6. Append the new node, save_admin_sll(), free_admin_sll().
 *
 * PARAMS:   (none — fully interactive)
 * OUTPUT:   (none — prints status messages to stdout)
 * SCHEMA:   (none)
 */
static void cmd_add_admin(void) {
    printf("\n╔══════════════════════════════════╗\n");
    printf(  "║        ADD NEW ADMIN ACCOUNT     ║\n");
    printf(  "╚══════════════════════════════════╝\n");

    /* ── Step 1: Load existing SLL ─────────────────────────────── */
    AdminNode* head = load_admin_sll();   /* NULL = file absent, that's OK */

    /* ── Step 2: Display what already exists ───────────────────── */
    printf("\n  Existing admins:\n");
    print_admin_table(head);
    printf("\n");

    /* ── Step 3: Generate next ID ──────────────────────────────── */
    int  count   = count_admins(head);
    int  next_num = 1001 + count;
    char new_id[MAX_ID_LEN];
    snprintf(new_id, MAX_ID_LEN, "A%d", next_num);
    printf("  New Admin ID will be: %s\n\n", new_id);

    /* ── Step 4: Collect fields from the operator ──────────────── */
    char username  [MAX_STR_LEN];
    char password  [MAX_STR_LEN];
    char admin_name[MAX_STR_LEN];
    char email     [MAX_STR_LEN];

    read_line("Username    : ", username,   MAX_STR_LEN);

    /* ── Step 5: Duplicate username check ──────────────────────── */
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

    /* ── Step 6: Build the new node ────────────────────────────── */
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

    /* ── Step 7: Append, save, free ────────────────────────────── */
    head = admin_sll_append(head, new_node);
    save_admin_sll(head);
    free_admin_sll(head);

    printf("\n  [OK] Admin \"%s\" (%s) added successfully.\n",
           username, new_id);
}

/*
 * FUNCTION: cmd_add_delivery_boy
 * PURPOSE:  Interactively add a new Delivery Boy to delivery_boys.dat.
 *
 * PIPELINE:
 *   1. Load the existing DeliveryBoy SLL via load_delivery_boy_sll().
 *   2. Display current boys so the operator has context.
 *   3. Calculate next ID: D(1001 + count).
 *   4. Prompt for: name, phone, vehicle_no.
 *   5. Prompt for is_active (defaults to 1).
 *   6. Reset ALL existing boys' last_assigned to 0.
 *   7. Set new boy's last_assigned to 1 (CLL restarts from boy #1).
 *   8. Append, save, free.
 *
 * PARAMS:   (none — fully interactive)
 * OUTPUT:   (none — prints status messages to stdout)
 * SCHEMA:   (none)
 */
static void cmd_add_delivery_boy(void) {
    printf("\n╔══════════════════════════════════╗\n");
    printf(  "║       ADD NEW DELIVERY BOY       ║\n");
    printf(  "╚══════════════════════════════════╝\n");

    /* ── Step 1: Load existing SLL ─────────────────────────────── */
    DeliveryBoyNode* head = load_delivery_boy_sll();

    /* ── Step 2: Display what already exists ───────────────────── */
    printf("\n  Existing delivery boys:\n");
    print_boy_table(head);
    printf("\n");

    /* ── Step 3: Generate next ID ──────────────────────────────── */
    int  count    = count_delivery_boys(head);
    int  next_num = 1001 + count;
    char new_id[MAX_ID_LEN];
    snprintf(new_id, MAX_ID_LEN, "D%d", next_num);
    printf("  New Delivery Boy ID will be: %s\n\n", new_id);

    /* ── Step 4: Collect fields from the operator ──────────────── */
    char name      [MAX_STR_LEN];
    char phone     [MAX_STR_LEN];
    char vehicle_no[MAX_STR_LEN];
    char active_buf[8];

    read_line("Name          : ", name,       MAX_STR_LEN);
    read_line("Phone         : ", phone,      MAX_STR_LEN);
    read_line("Vehicle No    : ", vehicle_no, MAX_STR_LEN);

    /* ── Step 5: is_active prompt ──────────────────────────────── */
    int is_active = 1;
    while (1) {
        read_line("Is Active? [1=Yes / 0=No] (default 1): ",
                  active_buf, sizeof(active_buf));

        if (strlen(active_buf) == 0 ||
            strcmp(active_buf, "1") == 0) {
            is_active = 1;
            break;
        } else if (strcmp(active_buf, "0") == 0) {
            is_active = 0;
            break;
        }
        printf("  [!] Please enter 1 or 0.\n");
    }

    /* ── Step 6: Reset last_assigned for ALL existing boys ──────── */
    DeliveryBoyNode* curr = head;
    while (curr) {
        curr->data.last_assigned = 0;
        curr = curr->next;
    }

    /* ── Step 7: Build new node with last_assigned = 1 ─────────── */
    DeliveryBoyNode* new_node =
        (DeliveryBoyNode*)malloc(sizeof(DeliveryBoyNode));
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
    new_node->data.last_assigned = 1;   /* CLL will start from boy #1 next */
    new_node->next = NULL;

    /* ── Step 8: Append, save, free ────────────────────────────── */
    head = boy_sll_append(head, new_node);
    save_delivery_boy_sll(head);
    free_delivery_boy_sll(head);

    printf("\n  [OK] Delivery Boy \"%s\" (%s) added successfully.\n",
           name, new_id);
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

/* ═════════════════════════════════════════════════════════════
   SECTION 6: MAIN — Interactive Menu
   ═════════════════════════════════════════════════════════════ */

/*
 * FUNCTION: main
 * PURPOSE:  Entry point. Presents a looping switch-case menu until
 *           the operator chooses to exit.
 * PARAMS:   argc, argv — unused (no CLI arguments expected)
 * OUTPUT:   (none — fully interactive terminal UI)
 * SCHEMA:   (none)
 */
int main(void) {
    system("chcp 65001"); // Fix Unicode on Windows

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
        printf(  "  │  [5]  Exit                  │\n");
        printf(  "  └─────────────────────────────┘\n");
        printf(  "  Choice: ");
        fflush(stdout);

        if (!fgets(choice_buf, sizeof(choice_buf), stdin)) break;
        choice_buf[strcspn(choice_buf, "\n")] = '\0';

        if (strlen(choice_buf) == sizeof(choice_buf) - 1)
            flush_stdin();

        int choice = atoi(choice_buf);

        switch (choice) {
            case 1:
                cmd_add_admin();
                break;

            case 2:
                cmd_add_delivery_boy();
                break;

            case 3:
                cmd_view_admins();
                break;

            case 4:
                cmd_view_delivery_boys();
                break;

            case 5:
                printf("\n  Goodbye.\n\n");
                running = 0;
                break;

            default:
                printf("\n  [!] Invalid choice. Enter 1–5.\n");
                break;
        }
    }

    return 0;
}