/*
 * auth.c - Fresh Picks: Authentication & Profile Logic (v4 — Binary Storage)
 * ===========================================================================
 * Called by Flask via subprocess.run(): ./auth <action> [arg1] [arg2] ...
 *
 * OUTPUT CONTRACT: Always "SUCCESS|<data>" or "ERROR|<reason>".
 *
 * COMMANDS (argv[1]):
 *   login_user       <username> <password>
 *   login_admin      <username> <password>
 *   register         <username> <password> <full_name> <email> <phone> <address>
 *   get_profile      <user_id>
 *   change_pass_user <user_id> <old_password> <new_password>
 *   change_pass_admin<admin_id> <old_password> <new_password>
 *   update_profile   <user_id> <field> <new_value>
 *
 * Team: CodeCrafters | Project: Fresh Picks | SDP-1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "models.h"


/* cmd_login_user — linear scan over user table: match by username+password, O(N) on table slots */
void cmd_login_user(const char *username, const char *password,
                    UserNode **user_table, int user_table_size) {
    if (!username || strlen(username) == 0) { PRINT_ERROR("Username required"); return; }
    if (!password || strlen(password) == 0) { PRINT_ERROR("Password required"); return; }
    if (!user_table)                         { PRINT_ERROR("No users found");    return; }

    /* Login matches by username, not ID — must scan all occupied slots */
    for (int i = 0; i < user_table_size; i++) {
        if (!user_table[i]) continue;
        if (strcmp(user_table[i]->data.username, username) == 0 &&
            strcmp(user_table[i]->data.password, password) == 0) {
            PRINT_SUCCESS(user_table[i]->data.user_id);
            return;
        }
    }

    PRINT_ERROR("Invalid username or password");
}


/* cmd_login_admin — linear scan over admin table: match by username+password, O(N) on table slots */
void cmd_login_admin(const char *username, const char *password,
                     AdminNode **admin_table) {
    if (!username || strlen(username) == 0) { PRINT_ERROR("Username required");        return; }
    if (!password || strlen(password) == 0) { PRINT_ERROR("Password required");        return; }
    if (!admin_table)                        { PRINT_ERROR("Admin database not found"); return; }

    /* Login matches by username, not ID — must scan all occupied slots */
    for (int i = 0; i < ADMIN_TABLE_SIZE; i++) {
        if (!admin_table[i]) continue;
        if (strcmp(admin_table[i]->data.username, username) == 0 &&
            strcmp(admin_table[i]->data.password, password) == 0) {
            printf("SUCCESS|%s|%s\n",
                   admin_table[i]->data.admin_id,
                   admin_table[i]->data.admin_name);
            return;
        }
    }

    PRINT_ERROR("Invalid admin credentials");
}


/* cmd_register_user — scan table for duplicate username, append new node, update table slot */
void cmd_register_user(const char *username,  const char *password,
                       const char *full_name, const char *email,
                       const char *phone,     const char *address,
                       UserNode ***user_table, int *user_table_size,
                       UserNode  **user_head) {
    if (!username  || strlen(username)  == 0) { PRINT_ERROR("Username required");  return; }
    if (!password  || strlen(password)  == 0) { PRINT_ERROR("Password required");  return; }
    if (!full_name || strlen(full_name) == 0) { PRINT_ERROR("Full name required"); return; }
    if (!email     || strlen(email)     == 0) { PRINT_ERROR("Email required");     return; }
    if (!phone     || strlen(phone)     == 0) { PRINT_ERROR("Phone required");     return; }
    if (!address   || strlen(address)   == 0) { PRINT_ERROR("Address required");   return; }

    /* Duplicate username check — must scan all slots since username != ID */
    for (int i = 0; i < *user_table_size; i++) {
        if (!(*user_table)[i]) continue;
        if (strcmp((*user_table)[i]->data.username, username) == 0) {
            PRINT_ERROR("Username already exists");
            return;
        }
    }

    /* Generate next user ID from current SLL count */
    int count    = sll_count_users(*user_head);
    int next_num = ID_BASE + count;
    char new_id[MAX_ID_LEN];
    snprintf(new_id, MAX_ID_LEN, "U%d", next_num);

    /* Build new User struct */
    User new_user;
    memset(&new_user, 0, sizeof(User));
    strncpy(new_user.user_id,   new_id,    MAX_ID_LEN  - 1);
    strncpy(new_user.username,  username,  MAX_STR_LEN - 1);
    strncpy(new_user.password,  password,  MAX_STR_LEN - 1);
    strncpy(new_user.full_name, full_name, MAX_STR_LEN - 1);
    strncpy(new_user.email,     email,     MAX_STR_LEN - 1);
    strncpy(new_user.phone,     phone,     MAX_STR_LEN - 1);
    strncpy(new_user.address,   address,   MAX_ADD_LEN - 1);

    /* Allocate and append new SLL node */
    UserNode *new_node = (UserNode *)malloc(sizeof(UserNode));
    if (!new_node) { PRINT_ERROR("Memory allocation failed"); return; }
    new_node->data = new_user;
    new_node->next = NULL;

    if (!(*user_head)) {
        *user_head = new_node;
    } else {
        UserNode *tail = *user_head;
        while (tail->next) tail = tail->next;
        tail->next = new_node;
    }

    /* Grow table if the new index exceeds current capacity */
    int new_idx = get_index_from_id(new_id);
    if (new_idx >= *user_table_size) {
        int new_size = *user_table_size + USER_GROWTH_SIZE;
        UserNode **tmp = (UserNode **)realloc(*user_table, new_size * sizeof(UserNode *));
        if (!tmp) { PRINT_ERROR("Memory allocation failed"); return; }
        memset(tmp + *user_table_size, 0, USER_GROWTH_SIZE * sizeof(UserNode *));
        *user_table      = tmp;       /* update main()'s pointer in-place */
        *user_table_size = new_size;
    }

    (*user_table)[new_idx] = new_node;
    save_user_sll(*user_head);
    PRINT_SUCCESS(new_id);
}


/* cmd_get_profile — O(1) lookup by user_id via user_table */
void cmd_get_profile(const char *user_id,
                     UserNode **user_table, int user_table_size) {
    if (!user_id || strlen(user_id) == 0) { PRINT_ERROR("User ID required"); return; }
    if (!user_table)                       { PRINT_ERROR("No users found");   return; }

    int idx = get_index_from_id(user_id);
    if (idx < 0 || idx >= user_table_size || !user_table[idx]) {
        PRINT_ERROR("User not found");
        return;
    }

    UserNode *match = user_table[idx];
    printf("SUCCESS|%s|%s|%s|%s|%s|%s\n",
           match->data.user_id,
           match->data.username,
           match->data.full_name,
           match->data.email,
           match->data.phone,
           match->data.address);
}


/* cmd_change_pass_user — O(1) lookup by user_id, verify old password, persist */
void cmd_change_pass_user(const char *user_id,
                          const char *old_password, const char *new_password,
                          UserNode **user_table, int user_table_size,
                          UserNode  *user_head) {
    if (!user_id      || strlen(user_id)      == 0) { PRINT_ERROR("User ID required");      return; }
    if (!old_password || strlen(old_password) == 0) { PRINT_ERROR("Old password required"); return; }
    if (!new_password || strlen(new_password) == 0) { PRINT_ERROR("New password required"); return; }
    if (!user_table)                                 { PRINT_ERROR("No users found");        return; }

    int idx = get_index_from_id(user_id);
    if (idx < 0 || idx >= user_table_size || !user_table[idx]) {
        PRINT_ERROR("User not found");
        return;
    }

    UserNode *match = user_table[idx];
    if (strcmp(match->data.password, old_password) != 0) {
        PRINT_ERROR("Old password is incorrect");
        return;
    }

    strncpy(match->data.password, new_password, MAX_STR_LEN - 1);
    match->data.password[MAX_STR_LEN - 1] = '\0';

    save_user_sll(user_head);
    PRINT_SUCCESS("Password changed successfully");
}


/* cmd_change_pass_admin — O(1) lookup by admin_id, verify old password, persist */
void cmd_change_pass_admin(const char *admin_id,
                           const char *old_password, const char *new_password,
                           AdminNode **admin_table,
                           AdminNode  *admin_head) {
    if (!admin_id     || strlen(admin_id)     == 0) { PRINT_ERROR("Admin ID required");      return; }
    if (!old_password || strlen(old_password) == 0) { PRINT_ERROR("Old password required");  return; }
    if (!new_password || strlen(new_password) == 0) { PRINT_ERROR("New password required");  return; }
    if (!admin_table)                                { PRINT_ERROR("Admin database not found"); return; }

    int idx = get_index_from_id(admin_id);
    if (idx < 0 || idx >= ADMIN_TABLE_SIZE || !admin_table[idx]) {
        PRINT_ERROR("Admin not found");
        return;
    }

    AdminNode *match = admin_table[idx];
    if (strcmp(match->data.password, old_password) != 0) {
        PRINT_ERROR("Old password is incorrect");
        return;
    }

    strncpy(match->data.password, new_password, MAX_STR_LEN - 1);
    match->data.password[MAX_STR_LEN - 1] = '\0';

    save_admin_sll(admin_head);
    PRINT_SUCCESS("Admin password changed successfully");
}


/* cmd_update_profile — O(1) lookup by user_id, update one field, persist */
void cmd_update_profile(const char *user_id, const char *field, const char *new_value,
                        UserNode **user_table, int user_table_size,
                        UserNode  *user_head) {
    if (!user_id   || strlen(user_id)   == 0) { PRINT_ERROR("User ID required");  return; }
    if (!field     || strlen(field)     == 0) { PRINT_ERROR("Field required");     return; }
    if (!new_value || strlen(new_value) == 0) { PRINT_ERROR("New value required"); return; }
    if (!user_table)                           { PRINT_ERROR("No users found");    return; }

    /* Validate field name before any lookup */
    int valid_field = (strcmp(field, "full_name") == 0 ||
                       strcmp(field, "email")     == 0 ||
                       strcmp(field, "phone")     == 0 ||
                       strcmp(field, "address")   == 0);
    if (!valid_field) { PRINT_ERROR("Unknown field"); return; }

    int idx = get_index_from_id(user_id);
    if (idx < 0 || idx >= user_table_size || !user_table[idx]) {
        PRINT_ERROR("User not found");
        return;
    }

    UserNode *match = user_table[idx];

    if (strcmp(field, "full_name") == 0) {
        strncpy(match->data.full_name, new_value, MAX_STR_LEN - 1);
        match->data.full_name[MAX_STR_LEN - 1] = '\0';
    } else if (strcmp(field, "email") == 0) {
        strncpy(match->data.email, new_value, MAX_STR_LEN - 1);
        match->data.email[MAX_STR_LEN - 1] = '\0';
    } else if (strcmp(field, "phone") == 0) {
        strncpy(match->data.phone, new_value, MAX_STR_LEN - 1);
        match->data.phone[MAX_STR_LEN - 1] = '\0';
    } else { /* address */
        strncpy(match->data.address, new_value, MAX_ADD_LEN - 1);
        match->data.address[MAX_ADD_LEN - 1] = '\0';
    }

    save_user_sll(user_head);
    PRINT_SUCCESS("Profile updated");
}


/* main — load SLLs, build tables once, dispatch, free everything at the bottom */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        PRINT_ERROR("No action specified. Usage: ./auth <action> [args...]");
        return 1;
    }

    /* ── Load SLLs (source of truth) ── */
    UserNode  *user_head  = load_user_sll();
    AdminNode *admin_head = load_admin_sll();

    /* ── Build pointer tables for O(1) access ── */
    int user_table_size = 0;
    UserNode  **user_table  = build_user_table(user_head, &user_table_size);
    AdminNode **admin_table = build_admin_table(admin_head);

    if (!user_table)  { PRINT_ERROR("Failed to build user table");  return 1; }
    if (!admin_table) { PRINT_ERROR("Failed to build admin table"); return 1; }

    const char *action = argv[1];

    if (strcmp(action, "login_user") == 0) {
        if (argc < 4) { PRINT_ERROR("Usage: login_user <username> <password>"); goto cleanup; }
        cmd_login_user(argv[2], argv[3], user_table, user_table_size);

    } else if (strcmp(action, "login_admin") == 0) {
        if (argc < 4) { PRINT_ERROR("Usage: login_admin <username> <password>"); goto cleanup; }
        cmd_login_admin(argv[2], argv[3], admin_table);

    } else if (strcmp(action, "register") == 0) {
        if (argc < 8) { PRINT_ERROR("Usage: register <username> <password> <full_name> <email> <phone> <address>"); goto cleanup; }
        cmd_register_user(argv[2], argv[3], argv[4], argv[5], argv[6], argv[7],
                          &user_table, &user_table_size, &user_head);

    } else if (strcmp(action, "get_profile") == 0) {
        if (argc < 3) { PRINT_ERROR("Usage: get_profile <user_id>"); goto cleanup; }
        cmd_get_profile(argv[2], user_table, user_table_size);

    } else if (strcmp(action, "change_pass_user") == 0) {
        if (argc < 5) { PRINT_ERROR("Usage: change_pass_user <user_id> <old_pass> <new_pass>"); goto cleanup; }
        cmd_change_pass_user(argv[2], argv[3], argv[4], user_table, user_table_size, user_head);

    } else if (strcmp(action, "change_pass_admin") == 0) {
        if (argc < 5) { PRINT_ERROR("Usage: change_pass_admin <admin_id> <old_pass> <new_pass>"); goto cleanup; }
        cmd_change_pass_admin(argv[2], argv[3], argv[4], admin_table, admin_head);

    } else if (strcmp(action, "update_profile") == 0) {
        if (argc < 5) { PRINT_ERROR("Usage: update_profile <user_id> <field> <new_value>"); goto cleanup; }
        cmd_update_profile(argv[2], argv[3], argv[4], user_table, user_table_size, user_head);

    } else {
        char err[MAX_STR_LEN];
        snprintf(err, sizeof(err), "Unknown action: %s", action);
        PRINT_ERROR(err);
    }

cleanup:
    /* ── Free tables then SLLs ── */
    free(user_table);
    free(admin_table);
    free_user_sll(user_head);
    free_admin_sll(admin_head);

    return 0;
}
