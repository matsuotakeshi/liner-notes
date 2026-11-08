CC?=cc
PYTHON?=python3
PREFIX?=/usr/local
DESTDIR?=
CFLAGS?=-O2 -g
CPPFLAGS?=
LDFLAGS?=
SQLITE_CFLAGS?=-I/usr/local/include
SQLITE_LIBS?=-L/usr/local/lib -lsqlite3
LDLIBS?=

all: runtime vaultctl memberctl dbconvert

test-doctor:
	$(PYTHON) tests/test_doctor.py

check: test-doctor

runtime test-adapters: src/board_link.inc src/link_schema.h

dbconvert: src/dbconvert.c src/storage_sqlite.c src/storage_sqlite.h src/storage_records.inc
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror src/dbconvert.c src/storage_sqlite.c -pthread -lcrypto $(SQLITE_LIBS) -o dbconvert

runtime: src/classic_commands.inc src/doors.c src/doors.h src/db_index.h src/features64.inc src/mail_attachments.h src/profile_store.h src/download.h src/userlog.h src/app_limits.h src/durable.h src/activity.h src/store_limits.h src/content_limits.h src/date_filter.h src/zmodem_host.c src/zmodem_host.h src/runtime.c src/download.c src/userlog.c src/activity.c src/board_features.inc src/board.c src/board.h src/members.c src/members.h src/profile_store.c src/board_policy.c src/board_policy.h src/catalog.c src/catalog.h src/ui.c src/menu.c src/menu.h src/ui.h src/messages.c src/messages.h src/locales/ja.def src/locales/en.def src/session.c src/session.h src/terminal.c src/terminal.h src/telnet.c src/telnet.h src/moderation.c src/extras.c src/vault.c src/vault_bridge.c src/identity_legacy.c src/vault_transfer.c src/transfer_line.c src/vault_worker.c src/chat.c src/moderation.h src/extras.h src/vault.h src/vault_bridge.h src/principal.h src/identity_legacy.h src/transfer.h src/transfer_line.h src/vault_worker.h src/chat.h src/chat_preferences.h src/mail_files.c src/mail_files.h src/mailbox.c src/mailbox.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -pthread src/doors.c src/zmodem_host.c src/runtime.c src/download.c src/userlog.c src/activity.c src/board.c src/members.c src/profile_store.c src/board_policy.c src/catalog.c src/ui.c src/menu.c src/messages.c src/session.c src/terminal.c src/telnet.c src/moderation.c src/extras.c src/vault.c src/vault_bridge.c src/identity_legacy.c src/vault_transfer.c src/transfer_line.c src/vault_worker.c src/chat.c src/mail_files.c src/mailbox.c -lutil -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o runtime

test-adapters: src/classic_commands.inc src/db_index.h src/features64.inc src/mail_attachments.h src/profile_store.h src/download.h src/userlog.h src/app_limits.h src/durable.h src/activity.h src/store_limits.h src/content_limits.h src/date_filter.h tests/test_adapters.c src/session.c src/session.h src/terminal.c src/terminal.h src/telnet.c src/telnet.h src/ui.c src/menu.c src/menu.h src/messages.c src/messages.h src/locales/ja.def src/locales/en.def src/download.c src/userlog.c src/activity.c src/board_features.inc src/board.c src/members.c src/profile_store.c src/board_policy.c src/board_policy.h src/catalog.c src/moderation.c src/extras.c src/vault.c src/vault_bridge.c src/identity_legacy.c src/vault_transfer.c src/transfer_line.c src/vault_worker.c src/chat.c src/moderation.h src/extras.h src/vault.h src/vault_bridge.h src/principal.h src/identity_legacy.h src/transfer.h src/transfer_line.h src/vault_worker.h src/chat.h src/chat_preferences.h src/mail_files.c src/mail_files.h src/mailbox.c src/mailbox.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -pthread -Isrc tests/test_adapters.c src/session.c src/terminal.c src/telnet.c src/ui.c src/menu.c src/messages.c src/download.c src/userlog.c src/activity.c src/board.c src/members.c src/profile_store.c src/board_policy.c src/catalog.c src/moderation.c src/extras.c src/vault.c src/vault_bridge.c src/identity_legacy.c src/vault_transfer.c src/transfer_line.c src/vault_worker.c src/chat.c src/mail_files.c src/mailbox.c -lutil -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o test-adapters

test: test-userlog test-chat runtime vaultctl test-adapters test-vault-core test-upload-core test-transfer-connector
	$(PYTHON) tests/test_userlog.py ./test-userlog
	./test-chat
	./test-adapters
	./test-transfer-connector
	$(PYTHON) tests/test_vault_core.py ./test-vault-core
	$(PYTHON) tests/test_vault.py ./runtime ./vaultctl
	$(PYTHON) tests/test_vault_catalog.py ./runtime ./vaultctl
	$(PYTHON) tests/test_vault_manage.py ./runtime ./vaultctl
	$(PYTHON) tests/test_vault_boards.py ./runtime ./vaultctl
	$(PYTHON) tests/test_upload_core.py ./test-upload-core
	$(PYTHON) tests/test_upload.py ./runtime ./vaultctl
	$(PYTHON) tests/test_network_transfer.py ./runtime ./vaultctl
	$(PYTHON) tests/test_binary_transfer.py ./runtime ./vaultctl
	$(PYTHON) tests/test_vault_worker.py ./runtime ./vaultctl
	$(PYTHON) tests/test_transfer_workflows.py
	$(PYTHON) tests/test_zmodem_workflows.py
	$(PYTHON) tests/test_zmodem_recovery.py
	$(PYTHON) tests/test_chat.py ./runtime
	$(PYTHON) tests/test_talk.py ./runtime
	$(PYTHON) tests/test_chat_controls.py ./runtime
	$(PYTHON) tests/test_chat_preferences.py ./runtime
	$(PYTHON) tests/test_chat_kick.py ./runtime
	$(PYTHON) tests/test_outside_talk.py ./runtime
	$(PYTHON) tests/test_stage67.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage67_database.py ./runtime
	$(PYTHON) tests/test_stage67_ui.py ./runtime ./vaultctl
	$(PYTHON) tests/test_news_contact.py ./runtime
	$(PYTHON) tests/test_large_content.py ./runtime
	$(PYTHON) tests/test_stage60.py ./runtime
	$(PYTHON) tests/test_stage60_ui.py ./runtime
	$(PYTHON) tests/test_stage60_safety.py ./runtime
	$(PYTHON) tests/test_stage59.py ./runtime ./vaultctl
	$(PYTHON) tests/test_log_retention.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage58.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage58_ui.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage57.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage57_limits.py ./runtime ./vaultctl
	$(PYTHON) tests/test_limits.py ./runtime
	$(PYTHON) tests/test_stage56_ui.py ./runtime
	$(PYTHON) tests/test_mail.py ./runtime
	$(PYTHON) tests/test_mail_attachments.py ./runtime
	$(PYTHON) tests/test_mail_draft.py ./runtime
	$(PYTHON) tests/test_mail_draft_notice.py ./runtime
	$(PYTHON) tests/test_mail_arrival.py ./runtime
	$(PYTHON) tests/test_mail_notify.py ./runtime
	$(PYTHON) tests/test_runtime.py ./runtime
	$(PYTHON) tests/test_listener_restart.py ./runtime
	$(PYTHON) tests/test_bind.py ./runtime
	$(PYTHON) tests/test_ssh_entry.py ./runtime
	$(PYTHON) tests/test_daemon.py ./runtime
	$(PYTHON) tests/test_board.py ./runtime
	$(PYTHON) tests/test_notes.py ./runtime
	$(PYTHON) tests/test_members.py ./runtime
	$(PYTHON) tests/test_catalog.py ./runtime
	$(PYTHON) tests/test_rights.py ./runtime
	$(PYTHON) tests/test_ui.py ./runtime
	$(PYTHON) tests/test_i18n.py ./runtime
	$(PYTHON) tests/test_telnet.py ./runtime
	$(PYTHON) tests/test_telnet_client.py ./runtime
	$(PYTHON) tests/test_moderation.py ./runtime
	$(PYTHON) tests/test_visibility.py ./runtime
	$(PYTHON) tests/test_suspension.py ./runtime
	$(PYTHON) tests/test_audit.py ./runtime
	$(PYTHON) tests/test_content_audit.py ./runtime
	$(PYTHON) tests/test_audit_filters.py ./runtime
	$(PYTHON) tests/test_menu_files.py ./runtime
	$(PYTHON) tests/test_password.py ./runtime
	$(PYTHON) tests/test_search.py ./runtime
	$(PYTHON) tests/test_pager.py ./runtime
	$(PYTHON) tests/test_preferences.py ./runtime
	$(PYTHON) tests/test_list_pager.py ./runtime
	$(PYTHON) tests/test_preview.py ./runtime
	$(PYTHON) tests/test_draft_edit.py ./runtime
	$(PYTHON) tests/test_draft_undo.py ./runtime
	$(PYTHON) tests/test_saved_draft.py ./runtime
	$(PYTHON) tests/test_draft_notice.py ./runtime
	$(PYTHON) tests/test_board_unread.py ./runtime
	$(PYTHON) tests/test_next_board.py ./runtime
	$(PYTHON) tests/test_thread_unread.py ./runtime
	$(PYTHON) tests/test_unread_threads.py ./runtime
	$(PYTHON) tests/test_login_unread.py ./runtime
	$(PYTHON) tests/test_help.py ./runtime
	$(PYTHON) tests/test_help_release.py ./runtime
	$(PYTHON) tests/test_extended.py ./runtime

vaultctl: src/vaultctl.c src/vault.c src/vault.h src/principal.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror src/vaultctl.c src/vault.c -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o vaultctl

test-vault-core: tests/test_vault_core.c src/vault.c src/vault.h src/principal.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_vault_core.c src/vault.c -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o test-vault-core

test-upload-core: tests/test_upload_core.c src/vault.c src/vault.h src/principal.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_upload_core.c src/vault.c -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o test-upload-core

test-transfer-connector: tests/test_transfer_connector.c src/transfer_line.c src/transfer_line.h src/transfer.h src/vault.h src/principal.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_transfer_connector.c src/transfer_line.c -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o test-transfer-connector

# Optional interoperability checks: use explicit trusted lrzsz 0.13.1 paths.
LRZ?=lrz
LSZ?=lsz
test-zmodem: runtime vaultctl test-zhost
	./test-zhost
	$(PYTHON) tests/test_system_control.py ./runtime ./vaultctl
	$(PYTHON) tests/test_zmodem.py ./runtime ./vaultctl $(LRZ) $(LSZ)
	$(PYTHON) tests/test_zmodem_menu.py ./runtime ./vaultctl $(LRZ) $(LSZ)
	$(PYTHON) tests/test_zmodem_mail.py ./runtime ./vaultctl $(LRZ) $(LSZ)
	$(PYTHON) tests/test_zmodem_batch_resume.py ./runtime ./vaultctl $(LRZ) $(LSZ)
	$(PYTHON) tests/test_zupload_retry.py ./runtime ./vaultctl $(LRZ) $(LSZ)
	$(PYTHON) tests/test_zmodem_resume_defaults.py ./runtime ./vaultctl $(LRZ) $(LSZ)

test-chat: tests/test_chat.c src/chat.c src/chat.h src/chat_preferences.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_chat.c src/chat.c src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o test-chat

test-zhost: tests/test_zhost.c src/zmodem_host.c src/zmodem_host.h src/download.h src/userlog.h src/app_limits.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_zhost.c src/zmodem_host.c src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o test-zhost

# Real-wire 1GiB/16-file tests plus sparse 16GiB staging boundary.
test-zmodem-limits: vaultctl
	$(PYTHON) tests/test_zmodem_limits.py $(LRZ) $(LSZ)
	$(PYTHON) tests/test_stage64_gib.py ./vaultctl

test-userlog: tests/test_userlog.c src/userlog.c src/userlog.h src/durable.h src/app_limits.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_userlog.c src/userlog.c -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o test-userlog

.PHONY: all test clean check test-stage61 test-legacy-transfers test-zmodem test-zmodem-limits
check: test test-stage61 test-maintenance test-stage63 test-stage64

.PHONY: test-maintenance
test-maintenance: runtime vaultctl memberctl
	$(PYTHON) tests/test_maintenance.py ./runtime ./vaultctl ./memberctl

test-stage61-userlog: tests/test_stage61_userlog.c src/userlog.c src/userlog.h src/durable.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_stage61_userlog.c src/userlog.c -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o test-stage61-userlog

test-stage61: runtime vaultctl test-stage61-userlog
	$(PYTHON) tests/test_userlog.py ./test-stage61-userlog
	$(PYTHON) tests/test_stage61.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage61_ui.py ./runtime
	$(PYTHON) tests/test_stage61_safety.py ./runtime ./vaultctl

test-legacy-transfers: runtime vaultctl
	$(PYTHON) tests/test_stage61_protocols.py $(LRZ) $(LSZ)
	$(PYTHON) tests/test_stage61_transfer_menu.py ./runtime ./vaultctl $(LRZ) $(LSZ)

clean:
	rm -f runtime vaultctl memberctl dbconvert generate-scale generate-members test-adapters test-chat test-vault-core test-upload-core test-transfer-connector test-userlog test-stage61-userlog test-zhost

memberctl: src/db_index.h src/memberctl.c src/members.c src/members.h src/app_limits.h src/store_limits.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror src/memberctl.c src/members.c -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o memberctl

.PHONY: test-stage63
test-stage63: runtime vaultctl memberctl generate-scale
	$(PYTHON) tests/test_state_compact.py
	$(PYTHON) tests/test_stage63_read_errors.py ./runtime
	$(PYTHON) tests/test_stage63.py ./runtime ./vaultctl ./memberctl
	$(PYTHON) tests/test_stage63_limits.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage63_memory.py ./runtime ./generate-scale

generate-scale: tests/generate_scale.c
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror tests/generate_scale.c src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o generate-scale

.PHONY: test-stage64 test-stage64-scale test-stage64-zmodem
test-stage64: runtime vaultctl memberctl
	$(PYTHON) tests/test_index_fault.py ./runtime
	$(PYTHON) tests/test_stage64_batch_names.py
	$(PYTHON) tests/test_stage64.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage64_details.py ./runtime
	$(PYTHON) tests/test_stage64_vault.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage64_persistence.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage64_reclaim.py ./runtime

generate-members: tests/generate_members.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror tests/generate_members.c src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o generate-members

test-stage64-scale: runtime generate-scale generate-members
	env LINER_TEST_START_TIMEOUT=180 python3 tests/test_stage63_memory.py ./runtime ./generate-scale
	$(PYTHON) tests/test_stage64_members.py ./runtime ./generate-members

test-stage64-zmodem: runtime vaultctl test-zhost
	./test-zhost
	$(PYTHON) tests/test_stage64_zmodem.py ./runtime ./vaultctl $(LRZ) $(LSZ)

# Native door and stage65 persistence/UI regression checks.
test-stage65: runtime vaultctl memberctl
	$(PYTHON) tests/test_stage65.py ./runtime
	$(PYTHON) tests/test_stage65_delete_recovery.py ./runtime
	$(PYTHON) tests/test_stage65_doors.py ./runtime
	$(PYTHON) tests/test_door_resize.py ./runtime
	$(PYTHON) tests/test_doors_generic.py ./runtime
	$(PYTHON) tests/test_doors_config.py ./runtime
	$(PYTHON) tests/test_stage65_terminal.py ./runtime
	$(PYTHON) tests/test_stage65_ui.py ./runtime
	$(PYTHON) tests/test_stage65_scan_telnet.py ./runtime
	$(PYTHON) tests/test_stage65_database.py ./runtime ./vaultctl

test-stage65-zmodem: runtime vaultctl
	$(PYTHON) tests/test_stage65_zmodem.py ./runtime ./vaultctl $(LRZ) $(LSZ)

check: test-stage65

vaultctl test-vault-core test-upload-core: src/db_index.h

# RTBBS-style menu contexts and safe write confirmation.
runtime test-adapters: src/classic_commands.inc
.PHONY: test-stage66
test-stage66: runtime vaultctl
	$(PYTHON) tests/test_stage66_commands.py ./runtime ./vaultctl
check: test-stage66

runtime test-adapters vaultctl test-vault-core test-upload-core test-transfer-connector test-chat test-zhost test-userlog test-stage61-userlog memberctl generate-scale generate-members: src/storage_records.inc src/storage_sqlite.c src/storage_sqlite.h src/runtime_limits.h src/classic_commands.inc

.PHONY: test-stage67
test-stage67: all
	$(PYTHON) tests/test_stage67.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage67_database.py ./runtime
	$(PYTHON) tests/test_stage67_ui.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage67_limits_checks.py ./runtime ./vaultctl

.PHONY: test-stage68
test-stage68: all
	$(PYTHON) tests/test_stage68.py ./runtime

.PHONY: test-stage69
test-stage69: all
	$(PYTHON) tests/test_stage69.py ./runtime ./vaultctl
	$(PYTHON) tests/test_stage69_ui.py ./runtime ./vaultctl

.PHONY: test-stage70 test-stage70-scale
test-stage70: all
	$(PYTHON) tests/test_stage70.py ./runtime
	$(PYTHON) tests/test_stage70_gather.py ./runtime
	$(PYTHON) tests/test_transfer_timeouts.py
test-stage70-scale: all
	$(PYTHON) tests/test_stage70_scale.py ./runtime

.PHONY: test-stage71
test-stage71: all
	$(PYTHON) tests/test_stage71.py ./runtime
	$(PYTHON) tests/test_stage71_reconcile.py ./runtime ./vaultctl
check: test-stage71

.PHONY: install-linux
install-linux: all
	$(PYTHON) scripts/install-linux.py --prefix "$(PREFIX)" --destdir "$(DESTDIR)"

# Optional Web connector tests; use a Python with webui/requirements.txt installed.
.PHONY: test-web
test-web: runtime vaultctl
	$(PYTHON) tests/test_web_connector.py ./runtime
	$(PYTHON) tests/test_web_blocks.py ./runtime ./vaultctl
	$(PYTHON) tests/test_web_http.py ./runtime ./vaultctl
	$(PYTHON) tests/test_web_transfers.py ./runtime ./vaultctl
	$(PYTHON) tests/test_web_install.py
	$(PYTHON) tests/test_web_errors.py

.PHONY: test-doors test-stage72
test-doors: runtime
	$(PYTHON) tests/test_door_resize.py ./runtime
	$(PYTHON) tests/test_doors_config.py ./runtime
	$(PYTHON) tests/test_doors_generic.py ./runtime
test-stage72: test-doors test-web

# The optional link tests need link-requirements.txt; web tests need aiohttp.
.PHONY: test-stage73
test-stage73: all
	$(PYTHON) tests/test_stage73_database.py ./runtime
	$(PYTHON) tests/test_serial_connector.py ./runtime
	$(PYTHON) tests/test_web_reports.py
	$(PYTHON) tests/test_link_store.py
	$(PYTHON) tests/test_link_tls.py
	$(PYTHON) tests/test_link_bbs.py ./runtime
	$(PYTHON) tests/test_link_service.py ./runtime

.PHONY: test-stage74 test-stage74-scale
test-stage74: all
	$(PYTHON) tests/test_stage74_links.py
	$(PYTHON) tests/test_stage74_link_bbs.py ./runtime

test-stage74-scale: runtime generate-scale
	$(PYTHON) tests/test_stage74_scale.py
	$(PYTHON) tests/test_stage74_scan.py ./runtime ./generate-scale
