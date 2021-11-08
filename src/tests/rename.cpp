#include "test.h"

using namespace std;

static const uint8_t sid_everyone[] = { 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 }; // S-1-1-0

static void set_rename_information(HANDLE h, bool replace_if_exists, HANDLE root_dir, const u16string_view& filename) {
    NTSTATUS Status;
    IO_STATUS_BLOCK iosb;
    vector<uint8_t> buf(offsetof(FILE_RENAME_INFORMATION, FileName) + (filename.length() * sizeof(char16_t)));
    auto& fri = *(FILE_RENAME_INFORMATION*)buf.data();

    fri.ReplaceIfExists = replace_if_exists;
    fri.RootDirectory = root_dir;
    fri.FileNameLength = filename.length() * sizeof(char16_t);
    memcpy(fri.FileName, filename.data(), fri.FileNameLength);

    Status = NtSetInformationFile(h, &iosb, &fri, buf.size(), FileRenameInformation);

    if (Status != STATUS_SUCCESS)
        throw ntstatus_error(Status);

    if (iosb.Information != 0)
        throw formatted_error("iosb.Information was {}, expected 0", iosb.Information);
}

static void set_rename_information_ex(HANDLE h, ULONG flags, HANDLE root_dir, const u16string_view& filename) {
    NTSTATUS Status;
    IO_STATUS_BLOCK iosb;
    vector<uint8_t> buf(offsetof(FILE_RENAME_INFORMATION_EX, FileName) + (filename.length() * sizeof(char16_t)));
    auto& fri = *(FILE_RENAME_INFORMATION_EX*)buf.data();

    fri.Flags = flags;
    fri.RootDirectory = root_dir;
    fri.FileNameLength = filename.length() * sizeof(char16_t);
    memcpy(fri.FileName, filename.data(), fri.FileNameLength);

    Status = NtSetInformationFile(h, &iosb, &fri, buf.size(), FileRenameInformationEx);

    if (Status != STATUS_SUCCESS)
        throw ntstatus_error(Status);

    if (iosb.Information != 0)
        throw formatted_error("iosb.Information was {}, expected 0", iosb.Information);
}

static void set_dacl(HANDLE h, ACCESS_MASK access) {
    NTSTATUS Status;
    SECURITY_DESCRIPTOR sd;
    array<uint8_t, sizeof(ACL) + offsetof(ACCESS_ALLOWED_ACE, SidStart) + sizeof(sid_everyone)> aclbuf;

    if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
        throw formatted_error("InitializeSecurityDescriptor failed (error {})", GetLastError());

    auto& acl = *(ACL*)aclbuf.data();

    if (!InitializeAcl(&acl, aclbuf.size(), ACL_REVISION))
        throw formatted_error("InitializeAcl failed (error {})", GetLastError());

    if (access != 0) {
        acl.AceCount = 1;

        auto& ace = *(ACCESS_ALLOWED_ACE*)((uint8_t*)aclbuf.data() + sizeof(ACL));

        ace.Header.AceType = ACCESS_ALLOWED_ACE_TYPE;
        ace.Header.AceFlags = 0;
        ace.Header.AceSize = offsetof(ACCESS_ALLOWED_ACE, SidStart) + sizeof(sid_everyone);
        ace.Mask = access;
        memcpy(&ace.SidStart, sid_everyone, sizeof(sid_everyone));
    }

    if (!SetSecurityDescriptorDacl(&sd, true, &acl, false))
        throw formatted_error("SetSecurityDescriptorDacl failed (error {})", GetLastError());

    Status = NtSetSecurityObject(h, DACL_SECURITY_INFORMATION, &sd);

    if (Status != STATUS_SUCCESS)
        throw ntstatus_error(Status);
}

void test_rename(const u16string& dir) {
    unique_handle h, h2;

    test("Create file", [&]() {
        h = create_file(dir + u"\\renamefile1", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamefile1";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamefile1\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"renamefile1";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        test("Rename file", [&]() {
            set_rename_information(h.get(), false, nullptr, dir + u"\\renamefile1b");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamefile1b";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamefile1b\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"renamefile1b";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        test("Check old directory entry not there", [&]() {
            u16string_view name = u"renamefile1";

            exp_status([&]() {
                query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);
            }, STATUS_NO_SUCH_FILE);
        });

        h.reset();
    }

    test("Create directory", [&]() {
        h = create_file(dir + u"\\renamedir1", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h) {
        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamedir1";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamefile1\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"renamedir1";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        test("Rename file", [&]() {
            set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir1b");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamedir1b";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamedir1b\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"renamedir1b";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        test("Check old directory entry not there", [&]() {
            u16string_view name = u"renamedir1";

            exp_status([&]() {
                query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);
            }, STATUS_NO_SUCH_FILE);
        });

        h.reset();
    }

    test("Create file", [&]() {
        h = create_file(dir + u"\\renamefile2", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamefile2";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamefile2\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"renamefile2";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        test("Rename file to same name", [&]() {
            set_rename_information(h.get(), false, nullptr, dir + u"\\renamefile2");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamefile2";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamefile2\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"renamefile2";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        h.reset();
    }

    test("Create file", [&]() {
        h = create_file(dir + u"\\renamefile3", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamefile3";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamefile3\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"renamefile3";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        test("Rename file to different case", [&]() {
            set_rename_information(h.get(), false, nullptr, dir + u"\\RENAMEFILE3");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\RENAMEFILE3";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\RENAMEFILE3\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"RENAMEFILE3";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        h.reset();
    }

    test("Create file 1", [&]() {
        create_file(dir + u"\\renamefile4a", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    test("Create file 2", [&]() {
        h = create_file(dir + u"\\renamefile4b", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Try renaming file 2 to file 1 without ReplaceIfExists set", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), false, nullptr, dir + u"\\renamefile4a");
            }, STATUS_OBJECT_NAME_COLLISION);
        });

        test("Rename file 2 to file 1", [&]() {
            set_rename_information(h.get(), true, nullptr, dir + u"\\renamefile4a");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamefile4a";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamefile4a\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"renamefile4a";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        h.reset();
    }

    test("Create file 1", [&]() {
        h2 = create_file(dir + u"\\renamefile5a", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    test("Create file 2", [&]() {
        h = create_file(dir + u"\\renamefile5b", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Try renaming file 2 to file 1 without ReplaceIfExists set", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), false, nullptr, dir + u"\\renamefile5a");
            }, STATUS_OBJECT_NAME_COLLISION);
        });

        test("Try renaming file 2 to file 1 with file 1 open", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), true, nullptr, dir + u"\\renamefile5a");
            }, STATUS_ACCESS_DENIED);
        });

        h.reset();
    }

    h2.reset();

    test("Create file 1", [&]() {
        create_file(dir + u"\\renamefile6a", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    test("Create file 2", [&]() {
        h = create_file(dir + u"\\renamefile6b", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Try renaming file 2 to file 1 uppercase without ReplaceIfExists set", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), false, nullptr, dir + u"\\RENAMEFILE6A");
            }, STATUS_OBJECT_NAME_COLLISION);
        });

        test("Rename file 2 to file 1 uppercase", [&]() {
            set_rename_information(h.get(), true, nullptr, dir + u"\\RENAMEFILE6A");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\RENAMEFILE6A";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\RENAMEFILE6A\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"RENAMEFILE6A";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        h.reset();
    }

    test("Create directory", [&]() {
        create_file(dir + u"\\renamedir7", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    test("Create file", [&]() {
        h = create_file(dir + u"\\renamefile7", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamefile7";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamefile7\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"renamefile7";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        test("Move file to subdir", [&]() {
            set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir7\\renamefile7a");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamedir7\\renamefile7a";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamedir7\\renamefile7a\".");
        });

        test("Check old directory entry gone", [&]() {
            u16string_view name = u"renamefile7";

            exp_status([&]() {
                query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);
            }, STATUS_NO_SUCH_FILE);
        });

        test("Check new directory entry", [&]() {
            u16string_view name = u"renamefile7a";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir + u"\\renamedir7", name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        test("Try overwriting directory with file without ReplaceIfExists set", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir7");
            }, STATUS_OBJECT_NAME_COLLISION);
        });

        test("Try overwriting directory with file with ReplaceIfExists set", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), true, nullptr, dir + u"\\renamedir7");
            }, STATUS_ACCESS_DENIED);
        });
    }

    test("Create directory 1", [&]() {
        create_file(dir + u"\\renamedir8", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    test("Create directory 2", [&]() {
        h = create_file(dir + u"\\renamedir8a", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    test("Create file", [&]() {
        create_file(dir + u"\\renamefile8", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h) {
        test("Check directory entry", [&]() {
            u16string_view name = u"renamedir8a";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamedir8a";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamedir8a\".");
        });

        test("Move directory to subdir", [&]() {
            set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir8\\renamedir8b");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamedir8\\renamedir8b";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamedir8\\renamedir8b\".");
        });

        test("Check old directory entry gone", [&]() {
            u16string_view name = u"renamedir8a";

            exp_status([&]() {
                query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);
            }, STATUS_NO_SUCH_FILE);
        });

        test("Check new directory entry", [&]() {
            u16string_view name = u"renamedir8b";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir + u"\\renamedir8", name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        test("Try overwriting file with directory without ReplaceIfExists set", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), false, nullptr, dir + u"\\renamefile8");
            }, STATUS_OBJECT_NAME_COLLISION);
        });

        test("Try overwriting file with directory with ReplaceIfExists set", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), true, nullptr, dir + u"\\renamefile8");
            }, STATUS_ACCESS_DENIED);
        });

        h.reset();
    }

    test("Create file", [&]() {
        h = create_file(dir + u"\\renamefile9", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    test("Create directory", [&]() {
        h2 = create_file(dir + u"\\renamedir9", FILE_LIST_DIRECTORY | FILE_ADD_FILE, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h && h2) {
        test("Check directory entry", [&]() {
            u16string_view name = u"renamefile9";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamefile9";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamefile9\".");
        });

        test("Move file via RootDirectory handle", [&]() {
            set_rename_information(h.get(), false, h2.get(), u"renamefile9");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamedir9\\renamefile9";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamedir9\\renamefile9\".");
        });

        test("Check old directory entry gone", [&]() {
            u16string_view name = u"renamefile9";

            exp_status([&]() {
                query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);
            }, STATUS_NO_SUCH_FILE);
        });

        test("Try checking new directory entry with handle still open", [&]() {
            u16string_view name = u"renamefile9";

            exp_status([&]() {
                query_dir<FILE_DIRECTORY_INFORMATION>(dir + u"\\renamedir9", name);
            }, STATUS_SHARING_VIOLATION);
        });

        h2.reset();

        test("Check new directory entry", [&]() {
            u16string_view name = u"renamefile9";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir + u"\\renamedir9", name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        h.reset();
    }

    test("Create directory", [&]() {
        h2 = create_file(dir + u"\\renamedir10", FILE_LIST_DIRECTORY | FILE_ADD_FILE, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    test("Create file", [&]() {
        h = create_file(dir + u"\\renamedir10\\renamefile10", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Try checking directory entry with handle open", [&]() {
            u16string_view name = u"renamefile10";

            exp_status([&]() {
                query_dir<FILE_DIRECTORY_INFORMATION>(dir + u"\\renamedir10", name);
            }, STATUS_SHARING_VIOLATION);
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamedir10\\renamefile10";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamedir10\\renamefile10\".");
        });

        test("Rename file via RootDirectory handle", [&]() {
            set_rename_information(h.get(), false, h2.get(), u"renamefile10a");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamedir10\\renamefile10a";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamedir10\\renamefile10a\".");
        });

        h2.reset();

        test("Check old directory entry gone", [&]() {
            u16string_view name = u"renamefile10";

            exp_status([&]() {
                query_dir<FILE_DIRECTORY_INFORMATION>(dir + u"\\renamedir10", name);
            }, STATUS_NO_SUCH_FILE);
        });

        test("Check new directory entry", [&]() {
            u16string_view name = u"renamefile10a";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir + u"\\renamedir10", name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });
    }

    test("Create directory", [&]() {
        h2 = create_file(dir + u"\\renamedir11", WRITE_DAC, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    test("Create file", [&]() {
        h = create_file(dir + u"\\renamedir11\\renamefile11", DELETE, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h && h2) {
        test("Set directory permissions", [&]() {
            set_dacl(h2.get(), SYNCHRONIZE | FILE_ADD_FILE);
        });

        h2.reset();

        test("Rename file", [&]() {
            set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir11\\renamefile11a");
        });

        h.reset();
    }

    test("Create directory", [&]() {
        h2 = create_file(dir + u"\\renamedir12", WRITE_DAC, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    test("Create file", [&]() {
        h = create_file(dir + u"\\renamedir12\\renamefile12", DELETE, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h && h2) {
        test("Clear directory permissions", [&]() {
            set_dacl(h2.get(), 0);
        });

        h2.reset();

        test("Try to rename file", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir12\\renamefile12a");
            }, STATUS_ACCESS_DENIED);
        });

        h.reset();
    }

    test("Create directory", [&]() {
        h2 = create_file(dir + u"\\renamedir13", WRITE_DAC, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    test("Create subdir", [&]() {
        h = create_file(dir + u"\\renamedir13\\renamesubdir13", DELETE, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h && h2) {
        test("Set directory permissions", [&]() {
            set_dacl(h2.get(), SYNCHRONIZE | FILE_ADD_SUBDIRECTORY);
        });

        h2.reset();

        test("Rename subdir", [&]() {
            set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir13\\renamesubdir13a");
        });

        h.reset();
    }

    test("Create directory", [&]() {
        h2 = create_file(dir + u"\\renamedir14", WRITE_DAC, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    test("Create subdir", [&]() {
        h = create_file(dir + u"\\renamedir14\\renamesubdir14", DELETE, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h && h2) {
        test("Clear directory permissions", [&]() {
            set_dacl(h2.get(), 0);
        });

        h2.reset();

        test("Try to rename subdir", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir14\\renamefile14a");
            }, STATUS_ACCESS_DENIED);
        });

        h.reset();
    }

    test("Create file", [&]() {
        h = create_file(dir + u"\\renamefile15", FILE_READ_DATA, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Try renaming file without DELETE access", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), false, nullptr, dir + u"\\renamefile15a");
            }, STATUS_ACCESS_DENIED);
        });

        h.reset();
    }

    test("Create directory", [&]() {
        h = create_file(dir + u"\\renamedir16", FILE_LIST_DIRECTORY, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h) {
        test("Try renaming directory without DELETE access", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir16a");
            }, STATUS_ACCESS_DENIED);
        });

        h.reset();
    }

    test("Create directory 1", [&]() {
        h2 = create_file(dir + u"\\renamedir17a", WRITE_DAC, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    test("Create file", [&]() {
        h = create_file(dir + u"\\renamedir17a\\file", DELETE, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h && h2) {
        test("Clear directory 1 permissions", [&]() {
            set_dacl(h2.get(), 0);
        });

        h2.reset();

        test("Create directory 2", [&]() {
            h2 = create_file(dir + u"\\renamedir17b", WRITE_DAC, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
        });

        if (h2) {
            test("Set directory 2 permissions", [&]() {
                set_dacl(h2.get(), SYNCHRONIZE | FILE_ADD_FILE);
            });

            h2.reset();
        }

        test("Move file to directory 2", [&]() {
            set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir17b\\file");
        });

        test("Try to move back to directory 1", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir17a\\file");
            }, STATUS_ACCESS_DENIED);
        });

        h.reset();
    }

    test("Create directory 1", [&]() {
        h2 = create_file(dir + u"\\renamedir18a", WRITE_DAC, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    test("Create subdir", [&]() {
        h = create_file(dir + u"\\renamedir18a\\subdir", DELETE, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h && h2) {
        test("Clear directory 1 permissions", [&]() {
            set_dacl(h2.get(), 0);
        });

        h2.reset();

        test("Create directory 2", [&]() {
            h2 = create_file(dir + u"\\renamedir18b", WRITE_DAC, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
        });

        if (h2) {
            test("Set directory 2 permissions", [&]() {
                set_dacl(h2.get(), SYNCHRONIZE | FILE_ADD_SUBDIRECTORY);
            });

            h2.reset();
        }

        test("Move file to directory 2", [&]() {
            set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir18b\\subdir");
        });

        test("Try to move back to directory 1", [&]() {
            exp_status([&]() {
                set_rename_information(h.get(), false, nullptr, dir + u"\\renamedir18a\\subdir");
            }, STATUS_ACCESS_DENIED);
        });

        h.reset();
    }

    // FIXME - permissions on destination when overwriting
    // FIXME - overwriting readonly file
    // FIXME - check invalid names (invalid characters, > 255 UTF-16, > 255 UTF-8, invalid UTF-16)

    // FIXME - check can't rename root directory?
}

void test_rename_ex(const u16string& dir) {
    unique_handle h, h2;

    // FileRenameInformationEx introduced with Windows 10 1709

    test("Create file 1", [&]() {
        create_file(dir + u"\\renamefileex1a", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    test("Create file 2", [&]() {
        h = create_file(dir + u"\\renamefileex1b", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Try renaming file 2 to file 1 without FILE_RENAME_REPLACE_IF_EXISTS set", [&]() {
            exp_status([&]() {
                set_rename_information_ex(h.get(), 0, nullptr, dir + u"\\renamefileex1a");
            }, STATUS_OBJECT_NAME_COLLISION);
        });

        test("Rename file 2 to file 1 with FILE_RENAME_REPLACE_IF_EXISTS", [&]() {
            set_rename_information_ex(h.get(), FILE_RENAME_REPLACE_IF_EXISTS, nullptr, dir + u"\\renamefileex1a");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\renamefileex1a";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\renamefileex1a\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"renamefileex1a";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        h.reset();
    }

    test("Create file 1", [&]() {
        h2 = create_file(dir + u"\\renamefileex2a", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    test("Create file 2", [&]() {
        h = create_file(dir + u"\\renamefileex2b", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Try renaming file 2 to file 1 without FILE_RENAME_REPLACE_IF_EXISTS", [&]() {
            exp_status([&]() {
                set_rename_information_ex(h.get(), 0, nullptr, dir + u"\\renamefileex2a");
            }, STATUS_OBJECT_NAME_COLLISION);
        });

        test("Try renaming file 2 to file 1 with FILE_RENAME_REPLACE_IF_EXISTS and file 1 open", [&]() {
            exp_status([&]() {
                set_rename_information_ex(h.get(), FILE_RENAME_REPLACE_IF_EXISTS, nullptr, dir + u"\\renamefileex2a");
            }, STATUS_ACCESS_DENIED);
        });

        h.reset();
    }

    h2.reset();

    test("Create file 1", [&]() {
        create_file(dir + u"\\renamefileex3a", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    test("Create file 2", [&]() {
        h = create_file(dir + u"\\renamefileex3b", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Try renaming file 2 to file 1 uppercase without FILE_RENAME_REPLACE_IF_EXISTS", [&]() {
            exp_status([&]() {
                set_rename_information_ex(h.get(), 0, nullptr, dir + u"\\RENAMEFILEEX3A");
            }, STATUS_OBJECT_NAME_COLLISION);
        });

        test("Rename file 2 to file 1 uppercase with FILE_RENAME_REPLACE_IF_EXISTS", [&]() {
            set_rename_information_ex(h.get(), FILE_RENAME_REPLACE_IF_EXISTS, nullptr, dir + u"\\RENAMEFILEEX3A");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\RENAMEFILEEX3A";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\RENAMEFILEEX3A\".");
        });

        test("Check directory entry", [&]() {
            u16string_view name = u"RENAMEFILEEX3A";

            auto items = query_dir<FILE_DIRECTORY_INFORMATION>(dir, name);

            if (items.size() != 1)
                throw formatted_error("{} entries returned, expected 1.", items.size());

            auto& fdi = *static_cast<const FILE_DIRECTORY_INFORMATION*>(items.front());

            if (fdi.FileNameLength != name.size() * sizeof(char16_t))
                throw formatted_error("FileNameLength was {}, expected {}.", fdi.FileNameLength, name.size() * sizeof(char16_t));

            if (name != u16string_view((char16_t*)fdi.FileName, fdi.FileNameLength / sizeof(char16_t)))
                throw runtime_error("FileName did not match.");
        });

        h.reset();
    }

    // FIXME - FILE_RENAME_POSIX_SEMANTICS
    // FIXME - FILE_RENAME_IGNORE_READONLY_ATTRIBUTE

    // FIXME - FILE_RENAME_SUPPRESS_PIN_STATE_INHERITANCE
    // FIXME - FILE_RENAME_SUPPRESS_STORAGE_RESERVE_INHERITANCE
    // FIXME - FILE_RENAME_NO_INCREASE_AVAILABLE_SPACE
    // FIXME - FILE_RENAME_NO_DECREASE_AVAILABLE_SPACE
    // FIXME - FILE_RENAME_FORCE_RESIZE_TARGET_SR
    // FIXME - FILE_RENAME_FORCE_RESIZE_SOURCE_SR
    // FIXME - FILE_RENAME_FORCE_RESIZE_SR
}