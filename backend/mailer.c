/*
 * mailer.c - Fresh Picks: Cross-platform SMTP mailer
 *              using libcurl's native SMTP + MIME APIs.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "models.h"
#include <curl/curl.h>


static void trim(char *s) {
    size_t start = 0;
    size_t end;
    size_t len;

    if (!s) return;

    len = strlen(s);
    while (start < len && isspace((unsigned char)s[start])) {
        start++;
    }
    if (start > 0) {
        memmove(s, s + start, len - start + 1);
    }

    len = strlen(s);
    end = len;
    while (end > 0 && isspace((unsigned char)s[end - 1])) {
        end--;
    }
    s[end] = '\0';
}

static void copy_value(char *dest, size_t dest_size, const char *src) {
    size_t len;

    if (!dest || dest_size == 0) return;
    if (!src) src = "";

    len = strlen(src);
    if (len >= dest_size) len = dest_size - 1;

    memmove(dest, src, len);
    dest[len] = '\0';
}

static const char *env_first(const char *a, const char *b, const char *c) {
    const char *value = NULL;

    if (a) {
        value = getenv(a);
        if (value && value[0]) return value;
    }
    if (b) {
        value = getenv(b);
        if (value && value[0]) return value;
    }
    if (c) {
        value = getenv(c);
        if (value && value[0]) return value;
    }
    return NULL;
}

static int sanitize_header_value(const char *input, char *output, size_t output_size) {
    size_t i;
    size_t out_len = 0;

    if (!output || output_size == 0) return 1;
    output[0] = '\0';
    if (!input) return 0;

    for (i = 0; input[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)input[i];

        if (ch == '\r' || ch == '\n') continue;
        if (ch < 32 && ch != '\t') continue;

        if (out_len + 1 >= output_size) return 1;
        output[out_len++] = (ch == '\t') ? ' ' : (char)ch;
    }

    output[out_len] = '\0';
    trim(output);
    return 0;
}

static int is_valid_email(const char *email) {
    const char *at;
    const char *p;

    if (!email || !email[0]) return 0;

    at = strchr(email, '@');
    if (!at || at == email || !at[1] || strchr(at + 1, '@')) return 0;

    for (p = email; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (isspace(ch) || ch < 32) return 0;
    }

    return strchr(at + 1, '.') != NULL;
}

static int has_credentials(const MailConfig *cfg) {
    return cfg &&
           is_valid_email(cfg->smtp_email) &&
           cfg->smtp_password[0] &&
           cfg->smtp_host[0] &&
           cfg->smtp_port[0] &&
           strcmp(cfg->smtp_email, "yourgmail@gmail.com") != 0 &&
           strcmp(cfg->smtp_password, "your16charapppassword") != 0;
}

static void load_config(MailConfig *cfg) {
    FILE *fp = NULL;
    char line[1024];
    const char *value = NULL;

    copy_value(cfg->smtp_email, sizeof(cfg->smtp_email), "");
    copy_value(cfg->smtp_password, sizeof(cfg->smtp_password), "");
    copy_value(cfg->smtp_host, sizeof(cfg->smtp_host), "smtp.gmail.com");
    copy_value(cfg->smtp_port, sizeof(cfg->smtp_port), "465");
    copy_value(cfg->sender_name, sizeof(cfg->sender_name), "FreshPicks Orders");

    fp = fopen("mailer.env", "r");
    if (!fp) fp = fopen("backend/mailer.env", "r");

    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            char *equals = NULL;
            char *key = NULL;
            char *field_value = NULL;

            if (line[0] == '#') continue;

            equals = strchr(line, '=');
            if (!equals) continue;

            *equals = '\0';
            key = line;
            field_value = equals + 1;
            trim(key);
            trim(field_value);

            if (strcmp(key, "SMTP_EMAIL") == 0) {
                copy_value(cfg->smtp_email, sizeof(cfg->smtp_email), field_value);
            } else if (strcmp(key, "SMTP_APP_PASSWORD") == 0 ||
                       strcmp(key, "SMTP_PASSWORD") == 0) {
                copy_value(cfg->smtp_password, sizeof(cfg->smtp_password), field_value);
            } else if (strcmp(key, "SMTP_HOST") == 0) {
                copy_value(cfg->smtp_host, sizeof(cfg->smtp_host), field_value);
            } else if (strcmp(key, "SMTP_PORT") == 0) {
                copy_value(cfg->smtp_port, sizeof(cfg->smtp_port), field_value);
            } else if (strcmp(key, "SENDER_NAME") == 0) {
                copy_value(cfg->sender_name, sizeof(cfg->sender_name), field_value);
            }
        }
        fclose(fp);
    }

    value = env_first("SMTP_EMAIL", "SMTP_USERNAME", "GMAIL_EMAIL");
    if (value) copy_value(cfg->smtp_email, sizeof(cfg->smtp_email), value);

    value = env_first("SMTP_APP_PASSWORD", "SMTP_PASSWORD", "GMAIL_APP_PASSWORD");
    if (value) copy_value(cfg->smtp_password, sizeof(cfg->smtp_password), value);

    value = env_first("SMTP_HOST", "MAILER_SMTP_HOST", NULL);
    if (value) copy_value(cfg->smtp_host, sizeof(cfg->smtp_host), value);

    value = env_first("SMTP_PORT", "MAILER_SMTP_PORT", NULL);
    if (value) copy_value(cfg->smtp_port, sizeof(cfg->smtp_port), value);

    value = env_first("SENDER_NAME", "MAILER_SENDER_NAME", NULL);
    if (value) copy_value(cfg->sender_name, sizeof(cfg->sender_name), value);
}

static int file_exists(const char *path) {
    FILE *fp;

    if (!path || !path[0]) return 0;
    fp = fopen(path, "rb");
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

static const char *filename_from_path(const char *path) {
    const char *slash = NULL;
    const char *backslash = NULL;
    const char *name = path;

    if (!path || !path[0]) return "attachment.bin";

    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');

    if (slash && backslash) {
        name = (slash > backslash) ? slash + 1 : backslash + 1;
    } else if (slash) {
        name = slash + 1;
    } else if (backslash) {
        name = backslash + 1;
    }

    return (name && name[0]) ? name : "attachment.bin";
}

static const char *mime_type_for_attachment(const char *path) {
    const char *ext = strrchr(path ? path : "", '.');

    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".pdf") == 0) return "application/pdf";
    if (strcmp(ext, ".txt") == 0) return "text/plain";
    if (strcmp(ext, ".csv") == 0) return "text/csv";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";

    return "application/octet-stream";
}

static int add_header(struct curl_slist **headers, const char *name, const char *value) {
    char header_line[1024];
    struct curl_slist *updated = NULL;

    if (!headers || !name || !value) return 1;
    if (snprintf(header_line, sizeof(header_line), "%s: %s", name, value) >= (int)sizeof(header_line)) {
        return 1;
    }

    updated = curl_slist_append(*headers, header_line);
    if (!updated) return 1;

    *headers = updated;
    return 0;
}

static int send_email_smtp(const MailConfig *cfg,
                           const char *recipient,
                           const char *subject,
                           const char *body_text,
                           const char *body_html,
                           const char *attachment_path) {
    CURL *curl = NULL;
    CURLcode code = CURLE_OK;
    CURLcode init_code = CURLE_OK;
    curl_mime *mime = NULL;
    curl_mime *alternative = NULL;
    curl_mimepart *part = NULL;
    struct curl_slist *headers = NULL;
    struct curl_slist *recipients = NULL;
    char url[MAX_URL_VALUE];
    char safe_subject[MAX_CFG_VALUE];
    char safe_sender_name[MAX_CFG_VALUE];
    char from_header[1024];
    char error_buffer[CURL_ERROR_SIZE];
    int rc = 1;

    if (!is_valid_email(recipient)) {
        printf("ERROR|Invalid recipient email address\n");
        return 1;
    }
    if (attachment_path && attachment_path[0] && !file_exists(attachment_path)) {
        printf("ERROR|Attachment file not found\n");
        return 1;
    }
    if (sanitize_header_value(subject, safe_subject, sizeof(safe_subject)) || !safe_subject[0]) {
        printf("ERROR|Invalid email subject\n");
        return 1;
    }
    if (sanitize_header_value(cfg->sender_name, safe_sender_name, sizeof(safe_sender_name)) ||
        !safe_sender_name[0]) {
        copy_value(safe_sender_name, sizeof(safe_sender_name), "FreshPicks Orders");
    }
    if (snprintf(from_header, sizeof(from_header), "%s <%s>", safe_sender_name, cfg->smtp_email) >=
        (int)sizeof(from_header)) {
        printf("ERROR|Sender header is too long\n");
        return 1;
    }

    if (snprintf(url, sizeof(url), "%s://%s:%s",
                 strcmp(cfg->smtp_port, "465") == 0 ? "smtps" : "smtp",
                 cfg->smtp_host, cfg->smtp_port) >= (int)sizeof(url)) {
        printf("ERROR|SMTP URL is too long\n");
        return 1;
    }

    init_code = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (init_code != CURLE_OK) {
        printf("ERROR|libcurl initialization failed: %s\n", curl_easy_strerror(init_code));
        return 1;
    }

    curl = curl_easy_init();
    if (!curl) {
        printf("ERROR|Could not initialize libcurl mailer\n");
        goto cleanup;
    }

    error_buffer[0] = '\0';
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERNAME, cfg->smtp_email);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg->smtp_password);
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, cfg->smtp_email);

    recipients = curl_slist_append(NULL, recipient);
    if (!recipients) {
        printf("ERROR|Could not allocate SMTP recipient list\n");
        goto cleanup;
    }
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    if (add_header(&headers, "To", recipient) ||
        add_header(&headers, "From", from_header) ||
        add_header(&headers, "Subject", safe_subject)) {
        printf("ERROR|Could not allocate email headers\n");
        goto cleanup;
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    mime = curl_mime_init(curl);
    alternative = curl_mime_init(curl);
    if (!mime || !alternative) {
        printf("ERROR|Could not build MIME message\n");
        goto cleanup;
    }

    part = curl_mime_addpart(alternative);
    if (!part ||
        curl_mime_data(part, body_text ? body_text : "", CURL_ZERO_TERMINATED) != CURLE_OK ||
        curl_mime_type(part, "text/plain; charset=utf-8") != CURLE_OK) {
        printf("ERROR|Could not create plain-text email body\n");
        goto cleanup;
    }

    part = curl_mime_addpart(alternative);
    if (!part ||
        curl_mime_data(part, body_html ? body_html : "", CURL_ZERO_TERMINATED) != CURLE_OK ||
        curl_mime_type(part, "text/html; charset=utf-8") != CURLE_OK) {
        printf("ERROR|Could not create HTML email body\n");
        goto cleanup;
    }

    part = curl_mime_addpart(mime);
    if (!part ||
        curl_mime_subparts(part, alternative) != CURLE_OK ||
        curl_mime_type(part, "multipart/alternative") != CURLE_OK) {
        printf("ERROR|Could not create email alternative body\n");
        goto cleanup;
    }
    alternative = NULL;

    if (attachment_path && attachment_path[0]) {
        part = curl_mime_addpart(mime);
        if (!part ||
            curl_mime_filedata(part, attachment_path) != CURLE_OK ||
            curl_mime_filename(part, filename_from_path(attachment_path)) != CURLE_OK ||
            curl_mime_type(part, mime_type_for_attachment(attachment_path)) != CURLE_OK ||
            curl_mime_encoder(part, "base64") != CURLE_OK) {
            printf("ERROR|Could not attach the requested file\n");
            goto cleanup;
        }
    }

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        printf("ERROR|SMTP send failed: %s\n",
               error_buffer[0] ? error_buffer : curl_easy_strerror(code));
        goto cleanup;
    }

    printf("SUCCESS|Email sent\n");
    rc = 0;

cleanup:
    if (mime) curl_mime_free(mime);
    if (alternative) curl_mime_free(alternative);
    if (headers) curl_slist_free_all(headers);
    if (recipients) curl_slist_free_all(recipients);
    if (curl) curl_easy_cleanup(curl);
    curl_global_cleanup();
    return rc;
}

static int build_otp_email(const char *purpose,
                           const char *otp_code,
                           const char *reference,
                           char *subject,
                           size_t subject_size,
                           char *body_text,
                           size_t body_text_size,
                           char *body_html,
                           size_t body_html_size) {
    char safe_reference[128];
    const char *display_reference = NULL;

    safe_reference[0] = '\0';
    if (reference && reference[0]) {
        if (sanitize_header_value(reference, safe_reference, sizeof(safe_reference))) {
            return 1;
        }
    }
    display_reference = safe_reference[0] ? safe_reference : "your order";

    if (strcmp(purpose, "register") == 0) {
        snprintf(subject, subject_size, "FreshPicks Registration OTP");
        snprintf(body_text, body_text_size,
                 "Dear Customer,\r\n\r\n"
                 "Thank you for registering with FreshPicks.\r\n\r\n"
                 "Your verification OTP is: %s\r\n\r\n"
                 "This OTP is valid for 10 minutes.\r\n"
                 "If you did not request this registration, please ignore this email.\r\n\r\n"
                 "Regards,\r\nFreshPicks\r\n",
                 otp_code);
        snprintf(body_html, body_html_size,
                 "<html><body style=\"font-family:Arial,sans-serif;color:#333;\">"
                 "<p>Dear Customer,</p>"
                 "<p>Thank you for registering with <strong>FreshPicks</strong>.</p>"
                 "<p>Your verification OTP is:</p>"
                 "<div style=\"display:inline-block;padding:12px 18px;border-radius:8px;"
                 "background:#f4f8fb;border:1px solid #d4e4f2;font-size:28px;"
                 "letter-spacing:0.25em;font-weight:700;color:#007acc;\">%s</div>"
                 "<p>This OTP is valid for <strong>10 minutes</strong>.</p>"
                 "<p>Regards,<br><strong>FreshPicks</strong></p>"
                 "</body></html>",
                 otp_code);
        return 0;
    }

    if (strcmp(purpose, "cancel_order") == 0) {
        snprintf(subject, subject_size, "FreshPicks Cancellation OTP - %s", display_reference);
        snprintf(body_text, body_text_size,
                 "Dear Customer,\r\n\r\n"
                 "We received a request to cancel order %s.\r\n\r\n"
                 "Your cancellation OTP is: %s\r\n\r\n"
                 "This OTP is valid for 10 minutes.\r\n\r\n"
                 "Regards,\r\nFreshPicks\r\n",
                 display_reference, otp_code);
        snprintf(body_html, body_html_size,
                 "<html><body style=\"font-family:Arial,sans-serif;color:#333;\">"
                 "<p>Dear Customer,</p>"
                 "<p>We received a request to cancel <strong>%s</strong>.</p>"
                 "<p>Your cancellation OTP is:</p>"
                 "<div style=\"display:inline-block;padding:12px 18px;border-radius:8px;"
                 "background:#fff5f5;border:1px solid #f0c2c2;font-size:28px;"
                 "letter-spacing:0.25em;font-weight:700;color:#dc3545;\">%s</div>"
                 "<p>This OTP is valid for <strong>10 minutes</strong>.</p>"
                 "<p>Regards,<br><strong>FreshPicks</strong></p>"
                 "</body></html>",
                 display_reference, otp_code);
        return 0;
    }

    if (strcmp(purpose, "password_change") == 0) {
        snprintf(subject, subject_size, "FreshPicks Password Change OTP");
        snprintf(body_text, body_text_size,
                 "Dear Customer,\r\n\r\n"
                 "We received a request to change your FreshPicks account password.\r\n\r\n"
                 "Your password-change OTP is: %s\r\n\r\n"
                 "This OTP is valid for 10 minutes.\r\n"
                 "If you did not request this change, please ignore this email.\r\n\r\n"
                 "Regards,\r\nFreshPicks\r\n",
                 otp_code);
        snprintf(body_html, body_html_size,
                 "<html><body style=\"font-family:Arial,sans-serif;color:#333;\">"
                 "<p>Dear Customer,</p>"
                 "<p>We received a request to change your <strong>FreshPicks</strong> account password.</p>"
                 "<p>Your password-change OTP is:</p>"
                 "<div style=\"display:inline-block;padding:12px 18px;border-radius:8px;"
                 "background:#f7f2ff;border:1px solid #d7c4ff;font-size:28px;"
                 "letter-spacing:0.25em;font-weight:700;color:#7b4dff;\">%s</div>"
                 "<p>This OTP is valid for <strong>10 minutes</strong>.</p>"
                 "<p>If you did not request this change, you can safely ignore this email.</p>"
                 "<p>Regards,<br><strong>FreshPicks</strong></p>"
                 "</body></html>",
                 otp_code);
        return 0;
    }

    return 1;
}

int main(int argc, char *argv[]) {
    MailConfig cfg;
    const char *recipient = NULL;
    const char *subject = NULL;
    const char *body_text = NULL;
    const char *body_html = NULL;
    const char *attachment = NULL;
    char otp_subject[256];
    char otp_text[2048];
    char otp_html[4096];

    load_config(&cfg);
    if (!has_credentials(&cfg)) {
        printf("ERROR|SMTP credentials missing. Update backend/mailer.env\n");
        return 1;
    }

    if (argc == 3) {
        recipient = argv[1];
        attachment = argv[2];
        if (!is_valid_email(recipient) || !attachment[0]) {
            printf("ERROR|Usage: mailer <recipient_email> <absolute_attachment_path>\n");
            return 1;
        }
        subject = "FreshPicks Order Confirmed - Receipt Attached";
        body_text =
            "Dear Customer,\r\n\r\n"
            "Thank you for shopping with FreshPicks.\r\n\r\n"
            "Please find your order receipt attached.\r\n\r\n"
            "Happy shopping,\r\nFreshPicks\r\n";
        body_html =
            "<html><body style=\"font-family:Arial,sans-serif;color:#333;\">"
            "<p>Dear Customer,</p>"
            "<p>Thank you for shopping with <strong>FreshPicks</strong>.</p>"
            "<p>Please find your order receipt attached.</p>"
            "<p>Happy shopping,<br><strong>FreshPicks</strong></p>"
            "</body></html>";
    } else if (argc >= 5 && strcmp(argv[1], "otp") == 0) {
        recipient = argv[2];
        if (!is_valid_email(recipient) ||
            build_otp_email(argv[4], argv[3], argc > 5 ? argv[5] : "",
                            otp_subject, sizeof(otp_subject),
                            otp_text, sizeof(otp_text),
                            otp_html, sizeof(otp_html))) {
            printf("ERROR|Usage: mailer otp <recipient_email> <otp_code> <register|cancel_order|password_change> [reference]\n");
            return 1;
        }
        subject = otp_subject;
        body_text = otp_text;
        body_html = otp_html;
    } else {
        printf("ERROR|Usage: mailer <recipient_email> <absolute_attachment_path> OR mailer otp <recipient_email> <otp_code> <register|cancel_order|password_change> [reference]\n");
        return 1;
    }

    return send_email_smtp(&cfg, recipient, subject, body_text, body_html, attachment);
}