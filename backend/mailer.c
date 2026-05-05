/*
 * Fresh Picks C Mailer
 * --------------------
 * Windows C implementation using CDO COM SMTP. No Python mail sending.
 *
 * Usage:
 *   mailer.exe <recipient_email> <absolute_path_to_pdf>
 *   mailer.exe otp <recipient_email> <otp_code> <register|cancel_order> [reference]
 *
 * Config:
 *   Put credentials in backend/mailer.env:
 *     SMTP_EMAIL=yourgmail@gmail.com
 *     SMTP_APP_PASSWORD=your16charapppassword
 *     SMTP_HOST=smtp.gmail.com
 *     SMTP_PORT=465
 *     SENDER_NAME=FreshPicks Orders
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <oleauto.h>
#endif

#define MAX_CFG_VALUE 512

typedef struct MailConfig {
    char smtp_email[MAX_CFG_VALUE];
    char smtp_password[MAX_CFG_VALUE];
    char smtp_host[MAX_CFG_VALUE];
    char smtp_port[MAX_CFG_VALUE];
    char sender_name[MAX_CFG_VALUE];
} MailConfig;

static void trim(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
}

static void copy_value(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0) return;
    if (!src) src = "";
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static const char *env_first(const char *a, const char *b, const char *c) {
    const char *value = getenv(a);
    if (value && value[0]) return value;
    value = b ? getenv(b) : NULL;
    if (value && value[0]) return value;
    value = c ? getenv(c) : NULL;
    if (value && value[0]) return value;
    return NULL;
}

static void load_config(MailConfig *cfg) {
    FILE *fp;
    char line[1024];

    copy_value(cfg->smtp_email, sizeof(cfg->smtp_email), "");
    copy_value(cfg->smtp_password, sizeof(cfg->smtp_password), "");
    copy_value(cfg->smtp_host, sizeof(cfg->smtp_host), "smtp.gmail.com");
    copy_value(cfg->smtp_port, sizeof(cfg->smtp_port), "465");
    copy_value(cfg->sender_name, sizeof(cfg->sender_name), "FreshPicks Orders");

    fp = fopen("mailer.env", "r");
    if (!fp) {
        fp = fopen("backend/mailer.env", "r");
    }
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            char *equals;
            char *key;
            char *value;

            if (line[0] == '#') continue;
            equals = strchr(line, '=');
            if (!equals) continue;

            *equals = '\0';
            key = line;
            value = equals + 1;
            trim(key);
            trim(value);

            if (strcmp(key, "SMTP_EMAIL") == 0) {
                copy_value(cfg->smtp_email, sizeof(cfg->smtp_email), value);
            } else if (strcmp(key, "SMTP_APP_PASSWORD") == 0) {
                copy_value(cfg->smtp_password, sizeof(cfg->smtp_password), value);
            } else if (strcmp(key, "SMTP_HOST") == 0) {
                copy_value(cfg->smtp_host, sizeof(cfg->smtp_host), value);
            } else if (strcmp(key, "SMTP_PORT") == 0) {
                copy_value(cfg->smtp_port, sizeof(cfg->smtp_port), value);
            } else if (strcmp(key, "SENDER_NAME") == 0) {
                copy_value(cfg->sender_name, sizeof(cfg->sender_name), value);
            }
        }
        fclose(fp);
    }

    copy_value(cfg->smtp_email, sizeof(cfg->smtp_email),
               env_first("SMTP_EMAIL", "SMTP_USERNAME", "GMAIL_EMAIL") ?: cfg->smtp_email);
    copy_value(cfg->smtp_password, sizeof(cfg->smtp_password),
               env_first("SMTP_APP_PASSWORD", "SMTP_PASSWORD", "GMAIL_APP_PASSWORD") ?: cfg->smtp_password);
}

static int is_valid_email(const char *email) {
    return email && email[0] && strchr(email, '@') != NULL;
}

static int has_credentials(const MailConfig *cfg) {
    return is_valid_email(cfg->smtp_email) &&
           cfg->smtp_password[0] &&
           strcmp(cfg->smtp_email, "yourgmail@gmail.com") != 0 &&
           strcmp(cfg->smtp_password, "your16charapppassword") != 0;
}

#ifdef _WIN32
static void write_base64_file(FILE *out, const char *path) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    FILE *in = fopen(path, "rb");
    unsigned char buf[3];
    size_t n;
    int line_len = 0;

    if (!in) return;

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        char encoded[4];
        encoded[0] = table[buf[0] >> 2];
        encoded[1] = table[((buf[0] & 0x03) << 4) | (n > 1 ? (buf[1] >> 4) : 0)];
        encoded[2] = (n > 1) ? table[((buf[1] & 0x0f) << 2) | (n > 2 ? (buf[2] >> 6) : 0)] : '=';
        encoded[3] = (n > 2) ? table[buf[2] & 0x3f] : '=';

        fwrite(encoded, 1, 4, out);
        line_len += 4;
        if (line_len >= 76) {
            fprintf(out, "\r\n");
            line_len = 0;
        }
    }
    if (line_len != 0) fprintf(out, "\r\n");
    fclose(in);
}

static int write_mime_file(const MailConfig *cfg,
                           const char *recipient,
                           const char *subject,
                           const char *body_text,
                           const char *body_html,
                           const char *attachment_path,
                           char *mime_path,
                           size_t mime_path_size) {
    char temp_dir[MAX_PATH];
    FILE *out;

    if (!GetTempPathA(sizeof(temp_dir), temp_dir)) {
        copy_value(temp_dir, sizeof(temp_dir), ".\\");
    }
    if (!GetTempFileNameA(temp_dir, "fpm", 0, mime_path)) {
        return 1;
    }
    mime_path[mime_path_size - 1] = '\0';

    out = fopen(mime_path, "wb");
    if (!out) return 1;

    fprintf(out, "From: %s <%s>\r\n", cfg->sender_name, cfg->smtp_email);
    fprintf(out, "To: %s\r\n", recipient);
    fprintf(out, "Subject: %s\r\n", subject);
    fprintf(out, "MIME-Version: 1.0\r\n");
    fprintf(out, "Content-Type: multipart/mixed; boundary=\"FP_MIXED_BOUNDARY\"\r\n\r\n");

    fprintf(out, "--FP_MIXED_BOUNDARY\r\n");
    fprintf(out, "Content-Type: multipart/alternative; boundary=\"FP_ALT_BOUNDARY\"\r\n\r\n");

    fprintf(out, "--FP_ALT_BOUNDARY\r\n");
    fprintf(out, "Content-Type: text/plain; charset=utf-8\r\n");
    fprintf(out, "Content-Transfer-Encoding: 8bit\r\n\r\n");
    fprintf(out, "%s\r\n\r\n", body_text);

    fprintf(out, "--FP_ALT_BOUNDARY\r\n");
    fprintf(out, "Content-Type: text/html; charset=utf-8\r\n");
    fprintf(out, "Content-Transfer-Encoding: 8bit\r\n\r\n");
    fprintf(out, "%s\r\n\r\n", body_html);
    fprintf(out, "--FP_ALT_BOUNDARY--\r\n");

    if (attachment_path && attachment_path[0]) {
        fprintf(out, "\r\n--FP_MIXED_BOUNDARY\r\n");
        fprintf(out, "Content-Type: application/pdf; name=\"FreshPicks_Receipt.pdf\"\r\n");
        fprintf(out, "Content-Transfer-Encoding: base64\r\n");
        fprintf(out, "Content-Disposition: attachment; filename=\"FreshPicks_Receipt.pdf\"\r\n\r\n");
        write_base64_file(out, attachment_path);
    }

    fprintf(out, "\r\n--FP_MIXED_BOUNDARY--\r\n");
    fclose(out);
    return 0;
}

static void append_quoted(char *cmd, size_t cmd_size, const char *value) {
    strncat(cmd, "\"", cmd_size - strlen(cmd) - 1);
    while (value && *value && strlen(cmd) + 3 < cmd_size) {
        if (*value == '"') strncat(cmd, "\\", cmd_size - strlen(cmd) - 1);
        strncat(cmd, value, 1);
        value++;
    }
    strncat(cmd, "\"", cmd_size - strlen(cmd) - 1);
}

static int run_curl_smtp(const MailConfig *cfg,
                         const char *recipient,
                         const char *mime_path) {
    char command[4096] = "";
    char user_pass[1200];
    char smtp_url[1400];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD exit_code = 1;

    snprintf(user_pass, sizeof(user_pass), "%s:%s", cfg->smtp_email, cfg->smtp_password);
    snprintf(smtp_url, sizeof(smtp_url), "smtps://%s:%s", cfg->smtp_host, cfg->smtp_port);

    strncat(command, "curl.exe --silent --show-error --ssl-reqd --url ", sizeof(command) - strlen(command) - 1);
    append_quoted(command, sizeof(command), smtp_url);
    strncat(command, " --user ", sizeof(command) - strlen(command) - 1);
    append_quoted(command, sizeof(command), user_pass);
    strncat(command, " --mail-from ", sizeof(command) - strlen(command) - 1);
    append_quoted(command, sizeof(command), cfg->smtp_email);
    strncat(command, " --mail-rcpt ", sizeof(command) - strlen(command) - 1);
    append_quoted(command, sizeof(command), recipient);
    strncat(command, " --upload-file ", sizeof(command) - strlen(command) - 1);
    append_quoted(command, sizeof(command), mime_path);

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        printf("ERROR|Could not start curl.exe SMTP sender\n");
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exit_code != 0) {
        printf("ERROR|curl.exe SMTP send failed with exit code %lu\n", (unsigned long)exit_code);
        return 1;
    }

    printf("SUCCESS|Email sent\n");
    return 0;
}

static int send_email_curl(const MailConfig *cfg,
                           const char *recipient,
                           const char *subject,
                           const char *body_text,
                           const char *body_html,
                           const char *attachment_path) {
    char mime_path[MAX_PATH];
    int rc;

    if (write_mime_file(cfg, recipient, subject, body_text, body_html,
                        attachment_path, mime_path, sizeof(mime_path))) {
        printf("ERROR|Could not create MIME email file\n");
        return 1;
    }

    rc = run_curl_smtp(cfg, recipient, mime_path);
    DeleteFileA(mime_path);
    return rc;
}

#if 0
static BSTR bstr_from_utf8(const char *text) {
    int wide_len;
    BSTR bstr;
    if (!text) text = "";
    wide_len = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    bstr = SysAllocStringLen(NULL, wide_len > 0 ? wide_len - 1 : 0);
    if (bstr && wide_len > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text, -1, bstr, wide_len);
    }
    return bstr;
}

static void utf8_from_bstr(BSTR bstr, char *dest, size_t dest_size) {
    int len;
    if (!dest || dest_size == 0) return;
    dest[0] = '\0';
    if (!bstr) return;
    len = WideCharToMultiByte(CP_UTF8, 0, bstr, -1, dest, (int)dest_size, NULL, NULL);
    if (len <= 0) dest[0] = '\0';
    dest[dest_size - 1] = '\0';
}

static HRESULT get_dispid(IDispatch *obj, const wchar_t *name, DISPID *id) {
    LPOLESTR names[1];
    names[0] = (LPOLESTR)name;
    return obj->lpVtbl->GetIDsOfNames(obj, &IID_NULL, names, 1,
                                      LOCALE_USER_DEFAULT, id);
}

static HRESULT dispatch_call(IDispatch *obj, const wchar_t *name, WORD flags,
                             VARIANT *args, UINT arg_count, VARIANT *result) {
    DISPID id;
    DISPPARAMS params;
    EXCEPINFO excep;
    HRESULT hr;

    hr = get_dispid(obj, name, &id);
    if (FAILED(hr)) return hr;

    memset(&params, 0, sizeof(params));
    params.rgvarg = args;
    params.cArgs = arg_count;

    VariantInit(result);
    memset(&excep, 0, sizeof(excep));
    hr = obj->lpVtbl->Invoke(obj, id, &IID_NULL, LOCALE_USER_DEFAULT,
                             flags, &params, result, &excep, NULL);
    if (hr == DISP_E_EXCEPTION) {
        char source[256];
        char description[768];
        utf8_from_bstr(excep.bstrSource, source, sizeof(source));
        utf8_from_bstr(excep.bstrDescription, description, sizeof(description));
        snprintf(LAST_COM_ERROR, sizeof(LAST_COM_ERROR), "%s%s%s",
                 source,
                 source[0] && description[0] ? ": " : "",
                 description);
    }
    SysFreeString(excep.bstrSource);
    SysFreeString(excep.bstrDescription);
    SysFreeString(excep.bstrHelpFile);
    return hr;
}

static HRESULT prop_put(IDispatch *obj, const wchar_t *name, VARIANT *value) {
    DISPID id;
    DISPID named = DISPID_PROPERTYPUT;
    DISPPARAMS params;
    HRESULT hr;

    hr = get_dispid(obj, name, &id);
    if (FAILED(hr)) return hr;

    memset(&params, 0, sizeof(params));
    params.rgvarg = value;
    params.cArgs = 1;
    params.rgdispidNamedArgs = &named;
    params.cNamedArgs = 1;

    return obj->lpVtbl->Invoke(obj, id, &IID_NULL, LOCALE_USER_DEFAULT,
                               DISPATCH_PROPERTYPUT, &params, NULL, NULL, NULL);
}

static HRESULT prop_get_dispatch(IDispatch *obj, const wchar_t *name, IDispatch **out) {
    VARIANT result;
    HRESULT hr = dispatch_call(obj, name, DISPATCH_PROPERTYGET, NULL, 0, &result);
    if (FAILED(hr)) return hr;
    if (result.vt != VT_DISPATCH || !result.pdispVal) {
        VariantClear(&result);
        return E_FAIL;
    }
    *out = result.pdispVal;
    return S_OK;
}

static HRESULT fields_set(IDispatch *fields, const char *key, VARIANT *value) {
    VARIANT args[1];
    VARIANT item;
    IDispatch *field = NULL;
    HRESULT hr;

    VariantInit(&args[0]);
    args[0].vt = VT_BSTR;
    args[0].bstrVal = bstr_from_utf8(key);

    hr = dispatch_call(fields, L"Item", DISPATCH_PROPERTYGET, args, 1, &item);
    VariantClear(&args[0]);
    if (FAILED(hr)) return hr;
    if (item.vt != VT_DISPATCH || !item.pdispVal) {
        VariantClear(&item);
        return E_FAIL;
    }

    field = item.pdispVal;
    hr = prop_put(field, L"Value", value);
    field->lpVtbl->Release(field);
    return hr;
}

static void variant_bstr(VARIANT *v, const char *value) {
    VariantInit(v);
    v->vt = VT_BSTR;
    v->bstrVal = bstr_from_utf8(value);
}

static void variant_i4(VARIANT *v, long value) {
    VariantInit(v);
    v->vt = VT_I4;
    v->lVal = value;
}

static void variant_bool(VARIANT *v, int value) {
    VariantInit(v);
    v->vt = VT_BOOL;
    v->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
}

static int send_email_cdo(const MailConfig *cfg,
                          const char *recipient,
                          const char *subject,
                          const char *body_text,
                          const char *body_html,
                          const char *attachment_path) {
    CLSID clsid;
    IDispatch *message = NULL;
    IDispatch *config = NULL;
    IDispatch *fields = NULL;
    VARIANT v;
    HRESULT hr;
    char from_header[1024];

    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("ERROR|COM initialization failed\n");
        return 1;
    }

    hr = CLSIDFromProgID(L"CDO.Message", &clsid);
    if (SUCCEEDED(hr)) {
        hr = CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                              &IID_IDispatch, (void **)&message);
    }
    if (FAILED(hr) || !message) {
        CoUninitialize();
        printf("ERROR|CDO.Message is not available on this Windows installation\n");
        return 1;
    }

    hr = prop_get_dispatch(message, L"Configuration", &config);
    if (SUCCEEDED(hr)) hr = prop_get_dispatch(config, L"Fields", &fields);

    if (SUCCEEDED(hr)) {
        variant_i4(&v, 2);
        hr = fields_set(fields, "http://schemas.microsoft.com/cdo/configuration/sendusing", &v);
        VariantClear(&v);
    }
    if (SUCCEEDED(hr)) {
        variant_bstr(&v, cfg->smtp_host);
        hr = fields_set(fields, "http://schemas.microsoft.com/cdo/configuration/smtpserver", &v);
        VariantClear(&v);
    }
    if (SUCCEEDED(hr)) {
        variant_i4(&v, atol(cfg->smtp_port));
        hr = fields_set(fields, "http://schemas.microsoft.com/cdo/configuration/smtpserverport", &v);
        VariantClear(&v);
    }
    if (SUCCEEDED(hr)) {
        variant_i4(&v, 1);
        hr = fields_set(fields, "http://schemas.microsoft.com/cdo/configuration/smtpauthenticate", &v);
        VariantClear(&v);
    }
    if (SUCCEEDED(hr)) {
        variant_bstr(&v, cfg->smtp_email);
        hr = fields_set(fields, "http://schemas.microsoft.com/cdo/configuration/sendusername", &v);
        VariantClear(&v);
    }
    if (SUCCEEDED(hr)) {
        variant_bstr(&v, cfg->smtp_password);
        hr = fields_set(fields, "http://schemas.microsoft.com/cdo/configuration/sendpassword", &v);
        VariantClear(&v);
    }
    if (SUCCEEDED(hr)) {
        variant_bool(&v, 1);
        hr = fields_set(fields, "http://schemas.microsoft.com/cdo/configuration/smtpusessl", &v);
        VariantClear(&v);
    }
    if (SUCCEEDED(hr)) {
        VARIANT result;
        hr = dispatch_call(fields, L"Update", DISPATCH_METHOD, NULL, 0, &result);
        VariantClear(&result);
    }

    snprintf(from_header, sizeof(from_header), "%s <%s>",
             cfg->sender_name, cfg->smtp_email);

    if (SUCCEEDED(hr)) {
        variant_bstr(&v, from_header);
        hr = prop_put(message, L"From", &v);
        VariantClear(&v);
    }
    if (SUCCEEDED(hr)) {
        variant_bstr(&v, recipient);
        hr = prop_put(message, L"To", &v);
        VariantClear(&v);
    }
    if (SUCCEEDED(hr)) {
        variant_bstr(&v, subject);
        hr = prop_put(message, L"Subject", &v);
        VariantClear(&v);
    }
    if (SUCCEEDED(hr)) {
        variant_bstr(&v, body_text);
        hr = prop_put(message, L"TextBody", &v);
        VariantClear(&v);
    }
    if (SUCCEEDED(hr)) {
        variant_bstr(&v, body_html);
        hr = prop_put(message, L"HTMLBody", &v);
        VariantClear(&v);
    }
    if (SUCCEEDED(hr) && attachment_path && attachment_path[0]) {
        VARIANT args[1];
        VARIANT result;
        variant_bstr(&args[0], attachment_path);
        hr = dispatch_call(message, L"AddAttachment", DISPATCH_METHOD, args, 1, &result);
        VariantClear(&args[0]);
        VariantClear(&result);
    }
    if (SUCCEEDED(hr)) {
        VARIANT result;
        hr = dispatch_call(message, L"Send", DISPATCH_METHOD, NULL, 0, &result);
        VariantClear(&result);
    }

    if (fields) fields->lpVtbl->Release(fields);
    if (config) config->lpVtbl->Release(config);
    if (message) message->lpVtbl->Release(message);
    CoUninitialize();

    if (FAILED(hr)) {
        if (LAST_COM_ERROR[0]) {
            printf("ERROR|CDO SMTP send failed: %s\n", LAST_COM_ERROR);
        } else {
            printf("ERROR|CDO SMTP send failed: 0x%08lx\n", (unsigned long)hr);
        }
        return 1;
    }

    printf("SUCCESS|Email sent\n");
    return 0;
}
#endif
#endif

static int build_otp_email(const char *purpose,
                           const char *otp_code,
                           const char *reference,
                           char *subject,
                           size_t subject_size,
                           char *body_text,
                           size_t body_text_size,
                           char *body_html,
                           size_t body_html_size) {
    const char *safe_reference = reference && reference[0] ? reference : "your order";

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
        snprintf(subject, subject_size, "FreshPicks Cancellation OTP - %s", safe_reference);
        snprintf(body_text, body_text_size,
                 "Dear Customer,\r\n\r\n"
                 "We received a request to cancel order %s.\r\n\r\n"
                 "Your cancellation OTP is: %s\r\n\r\n"
                 "This OTP is valid for 10 minutes.\r\n\r\n"
                 "Regards,\r\nFreshPicks\r\n",
                 safe_reference, otp_code);
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
                 safe_reference, otp_code);
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
            printf("ERROR|Usage: mailer <recipient_email> <absolute_pdf_path>\n");
            return 1;
        }
        subject = "FreshPicks Order Confirmed - Receipt Attached";
        body_text =
            "Dear Customer,\r\n\r\n"
            "Thank you for shopping with FreshPicks.\r\n\r\n"
            "Please find your order receipt attached as a PDF document.\r\n\r\n"
            "Happy shopping,\r\nFreshPicks\r\n";
        body_html =
            "<html><body style=\"font-family:Arial,sans-serif;color:#333;\">"
            "<p>Dear Customer,</p>"
            "<p>Thank you for shopping with <strong>FreshPicks</strong>.</p>"
            "<p>Please find your order receipt attached as a PDF document.</p>"
            "<p>Happy shopping,<br><strong>FreshPicks</strong></p>"
            "</body></html>";
    } else if (argc >= 5 && strcmp(argv[1], "otp") == 0) {
        recipient = argv[2];
        if (!is_valid_email(recipient) ||
            build_otp_email(argv[4], argv[3], argc > 5 ? argv[5] : "",
                            otp_subject, sizeof(otp_subject),
                            otp_text, sizeof(otp_text),
                            otp_html, sizeof(otp_html))) {
            printf("ERROR|Usage: mailer otp <recipient_email> <otp_code> <register|cancel_order> [reference]\n");
            return 1;
        }
        subject = otp_subject;
        body_text = otp_text;
        body_html = otp_html;
    } else {
        printf("ERROR|Usage: mailer <recipient_email> <absolute_pdf_path> OR mailer otp <recipient_email> <otp_code> <register|cancel_order> [reference]\n");
        return 1;
    }

#ifdef _WIN32
    return send_email_curl(&cfg, recipient, subject, body_text, body_html, attachment);
#else
    printf("ERROR|This C mailer build currently supports Windows only\n");
    return 1;
#endif
}
