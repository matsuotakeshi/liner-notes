SESSION_SOURCES= src/session.c src/session_ui.c src/session_rtbbs.c src/session_auth.c src/session_board.c src/session_chat.c src/session_classic.c src/session_commands.c src/session_connector.c src/session_editor.c src/session_extended.c src/session_mail.c src/session_members.c src/session_menu.c src/session_output.c src/session_parse.c src/session_reading.c src/session_results.c src/session_services.c src/session_settings.c src/session_transfer.c src/session_vault.c

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
PTY_LIBS?=-lutil
# SSH is opt-in at build time and disabled until --ssh-port is configured.
SSH_CFLAGS?=
SSH_LIBS?=


all: runtime vaultctl memberctl dbconvert liner-notes-door-lock

liner-notes-door-lock: src/door_lock_helper.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror src/door_lock_helper.c -o liner-notes-door-lock

runtime: liner-notes-door-lock

test-doctor:
	$(PYTHON) tests/test_doctor.py

check: test-doctor

test-doctor-runtime: runtime
	$(PYTHON) tests/test_doctor_runtime.py ./runtime

check: test-doctor-runtime test-package-menu

test-package-menu: runtime dbconvert
	$(PYTHON) tests/test_package_menu_flow.py ./runtime ./dbconvert

runtime test-adapters: src/board_link.inc src/link_schema.h

dbconvert: src/dbconvert.c src/storage_sqlite.c src/storage_sqlite.h src/storage_records.inc
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror src/dbconvert.c src/storage_sqlite.c -pthread -lcrypto $(SQLITE_LIBS) -o dbconvert

runtime: src/ssh_transport.c src/ssh_transport.h src/member_keys.h src/session_internal.h src/doors.c src/door_core.c src/door_config.c src/door_protocol.c src/doors.h src/door_lock.c src/door_lock.h src/db_index.h src/features64.inc src/mail_attachments.h src/profile_store.h src/download.h src/userlog.h src/app_limits.h src/durable.h src/activity.h src/store_limits.h src/content_limits.h src/date_filter.h src/zmodem_host.c src/zmodem_host.h src/runtime.c src/connection_core.c src/connection_protocol.c src/download.c src/download_core.c src/download_store.c src/userlog.c src/activity.c src/activity_core.c src/activity_store.c src/catalog_core.c src/catalog_group_core.c src/catalog_policy_core.c src/board_topology_core.c src/board_features.inc src/board.c src/article_core.c src/member_core.c src/authentication_core.c src/authentication_form.c src/link_core.c src/mail_core.c src/board.h src/members.c src/members.h src/profile_store.c src/board_policy.c src/board_policy.h src/catalog.c src/catalog.h src/ui.c src/menu.c src/menu.h src/ui.h src/messages.c src/messages.h src/locales/ja.def src/locales/en.def $(SESSION_SOURCES) src/session_core.c src/session_policy_core.c src/form_flow_core.c src/message_flow_core.c src/list_navigation_core.c src/list_navigation_protocol.c src/mail_navigation_core.c src/preference_core.c src/traversal_core.c src/traversal_protocol.c src/batch_core.c src/batch_protocol.c src/editor_commands.c src/editor_view.c src/connector_protocol.c src/classic_input.c src/terminal_pager.c src/connector_core.c src/notification_core.c src/editor_core.c src/session.h src/terminal.c src/terminal.h src/telnet.c src/telnet.h src/moderation.c src/extras.c src/vault.c src/vault_bridge.c src/vault_query_core.c src/vault_management_core.c src/identity_legacy.c src/vault_transfer.c src/transfer_line.c src/vault_worker.c src/chat.c src/chat_core.c src/moderation.h src/extras.h src/vault.h src/vault_bridge.h src/principal.h src/identity_legacy.h src/transfer.h src/transfer_line.h src/vault_worker.h src/chat.h src/chat_preferences.h src/mail_files.c src/mail_files.h src/mailbox.c src/mailbox.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -pthread $(SSH_CFLAGS) src/ssh_transport.c src/doors.c src/door_core.c src/door_config.c src/door_protocol.c src/door_lock.c src/zmodem_host.c src/runtime.c src/connection_core.c src/connection_protocol.c src/download.c src/download_core.c src/download_store.c src/userlog.c src/activity.c src/activity_core.c src/activity_store.c src/catalog_core.c src/catalog_group_core.c src/catalog_policy_core.c src/board_topology_core.c src/board.c src/article_core.c src/member_core.c src/authentication_core.c src/authentication_form.c src/link_core.c src/mail_core.c src/members.c src/profile_store.c src/board_policy.c src/catalog.c src/ui.c src/menu.c src/messages.c $(SESSION_SOURCES) src/session_core.c src/session_policy_core.c src/form_flow_core.c src/message_flow_core.c src/list_navigation_core.c src/list_navigation_protocol.c src/mail_navigation_core.c src/preference_core.c src/traversal_core.c src/traversal_protocol.c src/batch_core.c src/batch_protocol.c src/editor_commands.c src/editor_view.c src/connector_protocol.c src/classic_input.c src/terminal_pager.c src/connector_core.c src/notification_core.c src/editor_core.c src/terminal.c src/telnet.c src/moderation.c src/extras.c src/vault.c src/vault_bridge.c src/vault_query_core.c src/vault_management_core.c src/identity_legacy.c src/vault_transfer.c src/transfer_line.c src/vault_worker.c src/chat.c src/chat_core.c src/mail_files.c src/mailbox.c $(PTY_LIBS) -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) $(SSH_LIBS) -o runtime

test-adapters: src/session_internal.h src/db_index.h src/features64.inc src/mail_attachments.h src/profile_store.h src/download.h src/userlog.h src/app_limits.h src/durable.h src/activity.h src/store_limits.h src/content_limits.h src/date_filter.h tests/test_adapters.c tests/test_session_operations.c tests/test_session_ui.c $(SESSION_SOURCES) src/session_core.c src/session_policy_core.c src/form_flow_core.c src/message_flow_core.c src/list_navigation_core.c src/list_navigation_protocol.c src/mail_navigation_core.c src/preference_core.c src/traversal_core.c src/traversal_protocol.c src/batch_core.c src/batch_protocol.c src/editor_commands.c src/editor_view.c src/connector_protocol.c src/classic_input.c src/terminal_pager.c src/connector_core.c src/notification_core.c src/editor_core.c src/session.h src/terminal.c src/terminal.h src/telnet.c src/telnet.h src/ui.c src/menu.c src/menu.h src/messages.c src/messages.h src/locales/ja.def src/locales/en.def src/download.c src/download_core.c src/download_store.c src/userlog.c src/activity.c src/activity_core.c src/activity_store.c src/catalog_core.c src/catalog_group_core.c src/catalog_policy_core.c src/board_topology_core.c src/board_features.inc src/board.c src/article_core.c src/member_core.c src/authentication_core.c src/authentication_form.c src/link_core.c src/mail_core.c src/members.c src/profile_store.c src/board_policy.c src/board_policy.h src/catalog.c src/moderation.c src/extras.c src/vault.c src/vault_bridge.c src/vault_query_core.c src/vault_management_core.c src/identity_legacy.c src/vault_transfer.c src/transfer_line.c src/vault_worker.c src/chat.c src/chat_core.c src/moderation.h src/extras.h src/vault.h src/vault_bridge.h src/principal.h src/identity_legacy.h src/transfer.h src/transfer_line.h src/vault_worker.h src/chat.h src/chat_preferences.h src/mail_files.c src/mail_files.h src/mailbox.c src/mailbox.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -pthread -Isrc tests/test_adapters.c tests/test_session_operations.c tests/test_session_ui.c $(SESSION_SOURCES) src/session_core.c src/session_policy_core.c src/form_flow_core.c src/message_flow_core.c src/list_navigation_core.c src/list_navigation_protocol.c src/mail_navigation_core.c src/preference_core.c src/traversal_core.c src/traversal_protocol.c src/batch_core.c src/batch_protocol.c src/editor_commands.c src/editor_view.c src/connector_protocol.c src/classic_input.c src/terminal_pager.c src/connector_core.c src/notification_core.c src/editor_core.c src/terminal.c src/telnet.c src/ui.c src/menu.c src/messages.c src/download.c src/download_core.c src/download_store.c src/userlog.c src/activity.c src/activity_core.c src/activity_store.c src/catalog_core.c src/catalog_group_core.c src/catalog_policy_core.c src/board_topology_core.c src/board.c src/article_core.c src/member_core.c src/authentication_core.c src/authentication_form.c src/link_core.c src/mail_core.c src/members.c src/profile_store.c src/board_policy.c src/catalog.c src/moderation.c src/extras.c src/vault.c src/vault_bridge.c src/vault_query_core.c src/vault_management_core.c src/identity_legacy.c src/vault_transfer.c src/transfer_line.c src/vault_worker.c src/chat.c src/chat_core.c src/mail_files.c src/mailbox.c $(PTY_LIBS) -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o test-adapters

test: test-userlog test-chat runtime vaultctl test-adapters test-vault-core test-upload-core test-transfer-connector
	$(PYTHON) tests/test_userlog.py ./test-userlog
	./test-chat
	./test-adapters
	$(PYTHON) tests/test_session_operations.py ./test-adapters
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
	$(PYTHON) tests/test_chat_core.py ./runtime
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
	$(PYTHON) tests/test_mail_service.py ./runtime
	$(PYTHON) tests/test_mail_reading.py ./runtime
	$(PYTHON) tests/test_mail_resources.py ./runtime
	$(PYTHON) tests/test_mail_delivery.py ./runtime
	$(PYTHON) tests/test_editor_documents.py ./runtime
	$(PYTHON) tests/test_editor_core.py ./runtime
	$(PYTHON) tests/test_reading_settings.py ./runtime ./vaultctl
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
	$(PYTHON) tests/test_profile_service.py ./runtime
	$(PYTHON) tests/test_application_review.py ./runtime

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
	$(PYTHON) tests/test_zmodem_cleanup_wait.py
	$(PYTHON) tests/test_transfer_terminal.py
	./test-zhost
	$(PYTHON) tests/test_system_control.py ./runtime ./vaultctl
	$(PYTHON) tests/test_zmodem.py ./runtime ./vaultctl $(LRZ) $(LSZ)
	$(PYTHON) tests/test_zmodem_menu.py ./runtime ./vaultctl $(LRZ) $(LSZ)
	$(PYTHON) tests/test_zmodem_mail.py ./runtime ./vaultctl $(LRZ) $(LSZ)
	$(PYTHON) tests/test_zmodem_batch_resume.py ./runtime ./vaultctl $(LRZ) $(LSZ)
	$(PYTHON) tests/test_zupload_retry.py ./runtime ./vaultctl $(LRZ) $(LSZ)
	$(PYTHON) tests/test_zmodem_resume_defaults.py ./runtime ./vaultctl $(LRZ) $(LSZ)

test-chat: tests/test_chat.c src/chat.c src/chat_core.c src/chat.h src/chat_preferences.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_chat.c src/chat.c src/chat_core.c src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o test-chat

test-zhost: tests/test_zhost.c src/zmodem_host.c src/zmodem_host.h src/download.h src/userlog.h src/app_limits.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_zhost.c src/zmodem_host.c src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o test-zhost

# Real-wire 1GiB/16-file tests plus sparse 16GiB staging boundary.
test-zmodem-limits: vaultctl
	$(PYTHON) tests/test_zmodem_limits.py $(LRZ) $(LSZ)
	$(PYTHON) tests/test_stage64_gib.py ./vaultctl

test-userlog: tests/test_userlog.c src/userlog.c src/userlog.h src/durable.h src/app_limits.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_userlog.c src/userlog.c -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o test-userlog

.PHONY: all test clean check test-stage61 test-legacy-transfers test-zmodem test-zmodem-limits
check: test test-stage61 test-maintenance test-stage63 test-stage64 test-posix-locks

.PHONY: test-posix-locks
test-posix-locks:
	$(PYTHON) tests/test_posix_locks.py

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
	rm -f test-session-ui-lifecycle test-session-core test-session-policy-core test-form-flow-core test-message-flow-core test-list-navigation-core test-mail-navigation-core test-preference-core test-traversal-core test-batch-core test-editor-interface test-connector-protocol test-terminal-policy test-connector-core test-notification-core test-connection-core test-chat-core test-door-core test-editor-core test-link-core test-authentication-core test-vault-management-core test-vault-query-core test-download-core test-download-store test-activity-core test-activity-store test-catalog-group-core test-catalog-group-store test-board-topology-core test-catalog-policy-core test-catalog-core test-member-settings-store test-mail-core test-member-core test-article-core liner-notes-door-lock runtime vaultctl memberctl dbconvert generate-structured generate-scale generate-members test-adapters test-chat test-vault-core test-upload-core test-transfer-connector test-userlog test-stage61-userlog test-zhost

memberctl: src/db_index.h src/memberctl.c src/members.c src/members.h src/app_limits.h src/store_limits.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror src/memberctl.c src/members.c -lcrypto $(SQLITE_LIBS) $(LDLIBS) src/storage_sqlite.c $(SQLITE_CFLAGS) -pthread -lcrypto $(SQLITE_LIBS) -o memberctl

.PHONY: test-stage63
.PHONY: test-server-diagnostics
test-server-diagnostics:
	$(PYTHON) tests/test_server_diagnostics.py

test-stage63: test-server-diagnostics runtime vaultctl memberctl generate-scale
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
	$(PYTHON) tests/test_board_ownership.py ./runtime
	$(PYTHON) tests/test_catalog_management.py ./runtime
	$(PYTHON) tests/test_board_policy_core.py ./runtime
	$(PYTHON) tests/test_board_topology_core.py ./runtime
	$(PYTHON) tests/test_catalog_group_core.py ./runtime
	$(PYTHON) tests/test_activity_core.py ./runtime
	$(PYTHON) tests/test_download_core.py ./runtime ./vaultctl
	$(PYTHON) tests/test_vault_query_core.py ./runtime ./vaultctl
	$(PYTHON) tests/test_vault_management_core.py ./runtime ./vaultctl
	$(PYTHON) tests/test_authentication_core.py ./runtime
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
runtime test-adapters: src/session_internal.h
.PHONY: test-stage66
test-stage66: runtime vaultctl
	$(PYTHON) tests/test_stage66_commands.py ./runtime ./vaultctl
check: test-stage66

runtime test-adapters vaultctl test-vault-core test-upload-core test-transfer-connector test-chat test-zhost test-userlog test-stage61-userlog memberctl generate-scale generate-members: src/storage_records.inc src/storage_sqlite.c src/storage_sqlite.h src/runtime_limits.h src/session_internal.h

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
generate-structured: tests/generate_structured.c src/storage_sqlite.c src/storage_sqlite.h src/storage_records.inc
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/generate_structured.c src/storage_sqlite.c -pthread -lcrypto $(SQLITE_LIBS) $(LDLIBS) -o generate-structured

test-stage70-scale: all generate-structured
	$(PYTHON) tests/test_stage70_scale.py ./runtime ./generate-structured

.PHONY: test-stage71
test-stage71: all
	$(PYTHON) tests/test_resume_receipt.py
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
	$(PYTHON) tests/test_link_core.py ./runtime
	$(PYTHON) tests/test_link_service.py ./runtime

.PHONY: test-stage74 test-stage74-scale
test-stage74: all
	$(PYTHON) tests/test_stage74_links.py
	$(PYTHON) tests/test_stage74_link_bbs.py ./runtime

test-stage74-scale: runtime generate-scale
	$(PYTHON) tests/test_stage74_scale.py
	$(PYTHON) tests/test_stage74_scan.py ./runtime ./generate-scale

# Typed Core service tests run without a terminal, socket or live database.
.PHONY: test-article-core
runtime test-adapters: src/article_core.h src/article_repository.h
test-article-core: src/profile_store.h src/content_limits.h src/store_limits.h tests/test_article_core.c src/article_core.c src/article_core.h src/article_repository.h src/members.h src/catalog.h src/moderation.h src/board_policy.h src/extras.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_article_core.c src/article_core.c -o test-article-core
	./test-article-core
check: test-article-core

# Account Core tests: no terminal, credentials or live database.
.PHONY: test-member-core
runtime test-adapters: src/member_settings_store.inc src/member_settings_repository.h src/member_core.h src/application_repository.h
test-member-core: src/member_settings_repository.h tests/test_member_core.c src/member_core.c src/member_core.h src/application_repository.h src/profile_store.h src/catalog.h src/members.h src/moderation.h src/extras.h src/app_limits.h src/content_limits.h src/chat_preferences.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_member_core.c src/member_core.c -o test-member-core
	./test-member-core
check: test-member-core

# Mail composition/draft Core contract, with stubbed persistence.
.PHONY: test-mail-core
runtime test-adapters: src/mail_core.h src/mail_repository.h src/transfer_line.h
test-mail-core: src/transfer.h src/vault.h src/principal.h src/store_limits.h tests/test_mail_core.c src/mail_core.c src/mail_core.h src/mail_repository.h src/mailbox.h src/mail_files.h src/mail_attachments.h src/members.h src/moderation.h src/app_limits.h src/content_limits.h src/runtime_limits.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_mail_core.c src/mail_core.c -o test-mail-core
	./test-mail-core
check: test-mail-core

.PHONY: test-member-settings-store
test-member-settings-store: tests/test_member_settings_store.c src/member_settings_store.inc src/member_settings_repository.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_member_settings_store.c $(SQLITE_LIBS) -o test-member-settings-store
	./test-member-settings-store
check: test-member-settings-store

# Catalog management Core: no terminal, session or live journal.
.PHONY: test-catalog-core
runtime test-adapters: src/catalog_core.h
test-catalog-core: tests/test_catalog_core.c src/catalog_core.c src/catalog_core.h src/catalog.h src/members.h src/moderation.h src/board_policy.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_catalog_core.c src/catalog_core.c -o test-catalog-core
	./test-catalog-core
check: test-catalog-core

.PHONY: test-catalog-policy-core
runtime test-adapters: src/catalog_policy_core.h src/board_policy_repository.h
test-catalog-policy-core: tests/test_catalog_policy_core.c src/catalog_policy_core.c src/catalog_policy_core.h src/catalog_core.h src/board_policy.h src/board_policy_repository.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_catalog_policy_core.c src/catalog_policy_core.c -o test-catalog-policy-core
	./test-catalog-policy-core
check: test-catalog-policy-core

.PHONY: test-board-topology-core
runtime test-adapters: src/board_topology_core.h src/board_topology_repository.h
test-board-topology-core: tests/test_board_topology_core.c src/board_topology_core.c src/board_topology_core.h src/board_topology_repository.h src/catalog_core.h src/board_policy_repository.h src/board_policy.h src/extras.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_board_topology_core.c src/board_topology_core.c -o test-board-topology-core
	./test-board-topology-core
check: test-board-topology-core

.PHONY: test-catalog-group-core test-catalog-group-store
runtime test-adapters: src/catalog_group_core.h src/catalog_group_repository.h src/catalog_group_store.inc
test-catalog-group-core: tests/test_catalog_group_core.c src/catalog_group_core.c src/catalog_group_core.h src/catalog_group_repository.h src/catalog_core.h src/catalog.h src/members.h src/moderation.h src/app_limits.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_catalog_group_core.c src/catalog_group_core.c -o test-catalog-group-core
	./test-catalog-group-core
test-catalog-group-store: tests/test_catalog_group_store.c src/catalog_group_store.inc src/catalog_group_repository.h src/catalog.h src/members.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_catalog_group_store.c $(SQLITE_LIBS) -o test-catalog-group-store
	./test-catalog-group-store
check: test-catalog-group-core test-catalog-group-store

.PHONY: test-activity-core test-activity-store
runtime test-adapters: src/activity_core.h src/activity_repository.h
test-activity-core: tests/test_activity_core.c src/activity_core.c src/activity_core.h src/activity_repository.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_activity_core.c src/activity_core.c -o test-activity-core
	./test-activity-core
test-activity-store: tests/test_activity_store.c src/activity_store.c src/activity_core.h src/activity_repository.h src/durable.h
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_activity_store.c src/activity_store.c src/storage_sqlite.c -pthread -lcrypto $(SQLITE_LIBS) -o test-activity-store
	$(PYTHON) tests/test_activity_store.py ./test-activity-store
check: test-activity-core test-activity-store

runtime test-adapters: src/download_core.h src/download_repository.h

.PHONY: test-download-core test-download-store
test-download-core: tests/test_download_core.c src/download_core.c src/download.c src/download_core.h src/download_repository.h src/download.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_download_core.c src/download_core.c src/download.c -lcrypto -o test-download-core
	./test-download-core
test-download-store: tests/test_download_store.c src/download_store.c src/download_repository.h src/download.h src/durable.h src/storage_sqlite.c
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_download_store.c src/download_store.c src/storage_sqlite.c -pthread -lcrypto $(SQLITE_LIBS) -o test-download-store
	$(PYTHON) tests/test_download_store.py ./test-download-store
check: test-download-core test-download-store

# Typed vault read/query services, called by the existing worker adapter.
runtime test-adapters: src/vault_query_core.h
.PHONY: test-vault-query-core
test-vault-query-core: tests/test_vault_query_core.c src/vault_query_core.c src/vault_query_core.h src/vault.h src/runtime_limits.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_vault_query_core.c src/vault_query_core.c -o test-vault-query-core
	./test-vault-query-core
check: test-vault-query-core

# Vault mutations and board-management services.
runtime test-adapters: src/vault_management_core.h
.PHONY: test-vault-management-core
test-vault-management-core: tests/test_vault_management_core.c src/vault_management_core.c src/vault_management_core.h src/vault.h src/download.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_vault_management_core.c src/vault_management_core.c -o test-vault-management-core
	./test-vault-management-core
check: test-vault-management-core

# Authentication/registration services and legacy form decoding.
runtime test-adapters: src/authentication_core.h src/authentication_form.h src/authentication_repository.h
.PHONY: test-authentication-core
test-authentication-core: tests/test_authentication_core.c src/authentication_core.c src/authentication_form.c src/authentication_core.h src/authentication_form.h src/authentication_repository.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_authentication_core.c src/authentication_core.c src/authentication_form.c -lcrypto -o test-authentication-core
	./test-authentication-core
check: test-authentication-core

runtime test-adapters: src/link_core.h src/link_repository.h src/link_adapter.inc
.PHONY: test-link-core
test-link-core: tests/test_link_core.c src/link_core.c src/link_core.h src/link_repository.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_link_core.c src/link_core.c -lcrypto -o test-link-core
	./test-link-core
check: test-link-core

runtime test-adapters: src/editor_core.h
.PHONY: test-editor-core
test-editor-core: tests/test_editor_core.c src/editor_core.c src/editor_core.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_editor_core.c src/editor_core.c -lcrypto -o test-editor-core
	./test-editor-core
check: test-editor-core

# Door configuration, access-filtered catalog and launch selection without PTY.
runtime: src/door_core.h src/door_config.h src/door_protocol.h
.PHONY: test-door-core
test-door-core: tests/test_door_core.c src/door_core.c src/door_config.c src/door_protocol.c src/door_core.h src/door_config.h src/door_protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_door_core.c src/door_core.c src/door_config.c src/door_protocol.c -o test-door-core
	./test-door-core
check: test-door-core

# Explicit chat contexts and injectable public-log persistence.
runtime test-adapters test-chat: src/chat_core.h src/chat_types.h
.PHONY: test-chat-core
test-chat-core: tests/test_chat_core.c src/chat_core.c src/chat_core.h src/chat_types.h src/chat_preferences.h src/app_limits.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_chat_core.c src/chat_core.c -o test-chat-core
	./test-chat-core
check: test-chat-core

# Event-loop connection policy and legacy administrative report contract.
runtime: src/connection_core.h src/connection_protocol.h
.PHONY: test-connection-core
test-connection-core: tests/test_connection_core.c src/connection_core.c src/connection_protocol.c src/connection_core.h src/connection_protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_connection_core.c src/connection_core.c src/connection_protocol.c -o test-connection-core
	./test-connection-core
check: test-connection-core

# Session-local notification policy, independent of stores, transport and rendering.
runtime test-adapters: src/notification_core.h
.PHONY: test-notification-core
test-notification-core: tests/test_notification_core.c src/notification_core.c src/notification_core.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_notification_core.c src/notification_core.c -o test-notification-core
	./test-notification-core
check: test-notification-core

# Connector framing and bounded assembly have no UI, storage or socket dependency.
runtime test-adapters: src/connector_core.h
.PHONY: test-connector-core
test-connector-core: tests/test_connector_core.c src/connector_core.c src/connector_core.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_connector_core.c src/connector_core.c -o test-connector-core
	./test-connector-core
check: test-connector-core

# Terminal input/display policy is independent of Session and business Core.
runtime test-adapters: src/classic_input.h src/terminal_pager.h
.PHONY: test-terminal-policy
test-terminal-policy: tests/test_terminal_policy.c src/classic_input.c src/classic_input.h src/terminal_pager.c src/terminal_pager.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_terminal_policy.c src/classic_input.c src/terminal_pager.c -o test-terminal-policy
	./test-terminal-policy
check: test-terminal-policy

# Connector wire parsing and typed state snapshot (no Session/store dependency).
runtime test-adapters: src/connector_protocol.h
.PHONY: test-connector-protocol
test-connector-protocol: tests/test_connector_protocol.c src/connector_protocol.c src/connector_protocol.h src/store_limits.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_connector_protocol.c src/connector_protocol.c $(LDLIBS) -lcrypto -o test-connector-protocol
	./test-connector-protocol
check: test-connector-protocol

# Editor command/presentation boundaries share the existing document Core.
runtime test-adapters: src/editor_commands.h src/editor_view.h
.PHONY: test-editor-interface
test-editor-interface: tests/test_editor_interface.c src/editor_commands.c src/editor_commands.h src/editor_view.c src/editor_view.h src/editor_core.c src/editor_core.h src/store_limits.h src/content_limits.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_editor_interface.c src/editor_commands.c src/editor_view.c src/editor_core.c $(LDLIBS) -lcrypto -o test-editor-interface
	./test-editor-interface
check: test-editor-interface

# Per-session file selection and individual completion receipts.
runtime test-adapters: src/batch_core.h src/batch_protocol.h
.PHONY: test-batch-core
test-batch-core: tests/test_batch_core.c src/batch_core.c src/batch_core.h src/batch_protocol.c src/batch_protocol.h src/store_limits.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_batch_core.c src/batch_core.c src/batch_protocol.c -o test-batch-core
	./test-batch-core
check: test-batch-core

runtime test-adapters: src/traversal_core.h src/traversal_protocol.h
.PHONY: test-traversal-core
test-traversal-core: tests/test_traversal_core.c src/traversal_core.c src/traversal_core.h src/traversal_protocol.c src/traversal_protocol.h src/store_limits.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_traversal_core.c src/traversal_core.c src/traversal_protocol.c -o test-traversal-core
	./test-traversal-core
check: test-traversal-core

runtime test-adapters: src/preference_core.h
.PHONY: test-preference-core
test-preference-core: tests/test_preference_core.c src/preference_core.c src/preference_core.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_preference_core.c src/preference_core.c -o test-preference-core
	./test-preference-core
check: test-preference-core

# Session mail navigation and draft response policy, independent of storage/UI.
runtime test-adapters test-mail-core: src/mail_query.h src/mail_navigation_core.h
.PHONY: test-mail-navigation-core
test-mail-navigation-core: tests/test_mail_navigation_core.c src/mail_navigation_core.c src/mail_navigation_core.h src/mail_query.h src/traversal_core.c src/traversal_core.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_mail_navigation_core.c src/mail_navigation_core.c src/traversal_core.c -o test-mail-navigation-core
	./test-mail-navigation-core
check: test-mail-navigation-core

runtime test-adapters: src/list_navigation_core.h src/list_navigation_protocol.h
.PHONY: test-list-navigation-core
test-list-navigation-core: tests/test_list_navigation_core.c src/list_navigation_core.c src/list_navigation_core.h src/list_navigation_protocol.c src/list_navigation_protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_list_navigation_core.c src/list_navigation_core.c src/list_navigation_protocol.c -o test-list-navigation-core
	./test-list-navigation-core
check: test-list-navigation-core

runtime test-adapters: src/message_flow_core.h
.PHONY: test-message-flow-core
test-message-flow-core: tests/test_message_flow_core.c src/message_flow_core.c src/message_flow_core.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_message_flow_core.c src/message_flow_core.c -o test-message-flow-core
	./test-message-flow-core
check: test-message-flow-core

runtime test-adapters: src/form_flow_core.h
.PHONY: test-form-flow-core
test-form-flow-core: tests/test_form_flow_core.c src/form_flow_core.c src/form_flow_core.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_form_flow_core.c src/form_flow_core.c -o test-form-flow-core
	./test-form-flow-core
check: test-form-flow-core

runtime test-adapters: src/session_policy_core.h
.PHONY: test-session-policy-core test-session-boundaries
test-session-policy-core: tests/test_session_policy_core.c src/session_policy_core.c src/session_policy_core.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_session_policy_core.c src/session_policy_core.c -o test-session-policy-core
	./test-session-policy-core
test-session-boundaries:
	$(PYTHON) tests/test_session_boundaries.py
	$(PYTHON) tests/test_session_operations_harness.py
check: test-session-policy-core test-session-boundaries

runtime test-adapters: src/session_core.h src/session_state.h

.PHONY: test-session-core
test-session-core: tests/test_session_core.c src/session_core.c src/session_core.h src/board.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_session_core.c src/session_core.c -o test-session-core
	./test-session-core
check: test-session-core

# Shared data contracts are independent of the worker lifecycle.
runtime test-adapters test-session-core: src/operation.h src/operation_pages.h

runtime test-adapters: src/session_ui.h src/session_state.h src/session_io.h

.PHONY: test-session-ui-lifecycle
test-session-ui-lifecycle: tests/test_session_ui_lifecycle.c src/session.c src/session_ui.c src/session_core.c src/connector_core.c src/terminal_pager.c src/session.h src/session_ui.h src/session_io.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_session_ui_lifecycle.c src/session.c src/session_ui.c src/session_core.c src/connector_core.c src/terminal_pager.c -lcrypto $(LDLIBS) -o test-session-ui-lifecycle
	./test-session-ui-lifecycle
check: test-session-ui-lifecycle

# Lightweight checks on every supported host; encrypted interoperability is
# added to existing Hosted Linux CI with libssh and OpenSSH, not LAN PR runners.
test-ssh-sources:
	$(PYTHON) tests/test_ssh_sources.py

check: test-ssh-sources

.PHONY: test-storage-relations
check: test-storage-relations
test-storage-relations: tests/test_storage_relations.c src/storage_records.inc
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_storage_relations.c -lcrypto $(SQLITE_LIBS) -o test-storage-relations
	./test-storage-relations
	$(PYTHON) tests/test_structured_relations.py
.PHONY: test-ui-selection
test-ui-selection: runtime
	$(PYTHON) tests/test_ui_selection.py ./runtime
check: test-ui-selection
# All Door launch paths rebuild when the opt-in isolation adapter changes.
runtime: src/door_isolation.inc
runtime: src/door_diagnostics.h

.PHONY: test-door-broker
check: test-door-broker
test-door-broker:
	$(PYTHON) tests/test_door_worker_diagnostics.py
	$(PYTHON) tests/test_door_diagnostics.py
	$(PYTHON) tests/test_door_broker.py
	$(PYTHON) tests/test_door_processes.py
	$(PYTHON) tests/test_door_isolation_admission.py
	$(PYTHON) tests/test_door_services.py

# Generated schema descriptors and every storage consumer use the same codec.
runtime dbconvert test-adapters vaultctl test-vault-core test-upload-core test-transfer-connector test-chat test-zhost test-userlog test-stage61-userlog memberctl generate-structured generate-scale generate-members test-storage-relations: src/storage_normalized.inc src/storage_normalized_fields.inc src/storage_vault.inc
.PHONY: test-storage-normalized
test-storage-normalized: tests/test_storage_normalized.c src/storage_records.inc src/storage_normalized.inc src/storage_normalized_fields.inc src/storage_vault.inc
	$(PYTHON) scripts/normalized_fields.py --check
	$(CC) $(CPPFLAGS) $(SQLITE_CFLAGS) $(CFLAGS) $(LDFLAGS) -std=c11 -Wall -Wextra -Werror -Isrc tests/test_storage_normalized.c -lcrypto $(SQLITE_LIBS) -o test-storage-normalized
	./test-storage-normalized
	$(PYTHON) tests/test_storage_normalized.py
check: test-storage-normalized

# The authoritative codec changed: exercise scale/RSS once in existing check.
check: test-stage70-scale
