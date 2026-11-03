if (${BUILD_EXTENSION_TESTS})
    add_lbug_test(fts_prepare_test prepare_test.cpp)
    add_lbug_test(fts_read_only_test read_only_test.cpp)
    # These cases reopen the database and expect the extension to load with it, as a
    # statically linked one does.
    if ("fts" IN_LIST STATICALLY_LINKED_EXTENSIONS)
        add_lbug_test(fts_recovery_test recovery_test.cpp)
    endif ()
    add_lbug_test(fts_tokenizer_test tokenizer_test.cpp)
endif ()
