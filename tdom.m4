
#------------------------------------------------------------------------
# TDOM_ENABLE_DTD --
#
#   Allows the building with dtd
#
# Arguments:
#   none
#   
# Results:
#
#   Adds the following arguments to configure:
#       --enable-dtd=yes|no
#
#   Defines the following vars:
#
#   Sets the following vars:
#
#------------------------------------------------------------------------

AC_DEFUN(TDOM_ENABLE_DTD, [
    AC_MSG_CHECKING([wether to enable dtd support])
    AC_ARG_ENABLE(dtd,
    [  --enable-dtd            build with the dtd support [--enable-dtd]],
    [tcl_ok=$enableval], [tcl_ok=yes])

    if test "${enable_dt+set}" = set; then
        enableval="$enable_dtd"
        tcl_ok=$enableval
    else
        tcl_ok=yes
    fi

    if test "$tcl_ok" = "yes" ; then
        AC_MSG_RESULT([yes])
        AC_DEFINE(XML_DTD)
    else
        AC_MSG_RESULT([no])
    fi
])

#------------------------------------------------------------------------
# TDOM_ENABLE_NS --
#
#   Allows the building with namespace support
#
# Arguments:
#   none
#   
# Results:
#
#   Adds the following arguments to configure:
#       --enable-ns=yes|no
#
#   Defines the following vars:
#
#   Sets the following vars:
#
#------------------------------------------------------------------------

AC_DEFUN(TDOM_ENABLE_NS, [
    AC_MSG_CHECKING([wether to enable namespace support])
    AC_ARG_ENABLE(ns,
    [  --enable-ns             build with the namespace support [--enable-ns]],
    [tcl_ok=$enableval], [tcl_ok=yes])

    if test "${enable_ns+set}" = set; then
        enableval="$enable_ns"
        tcl_ok=$enableval
    else
        tcl_ok=yes
    fi

    if test "$tcl_ok" = "yes" ; then
        AC_MSG_RESULT([yes])
        AC_DEFINE(XML_NS)
    else
        AC_MSG_RESULT([no])
    fi
])

#------------------------------------------------------------------------
# TDOM_ENABLE_UNKNOWN --
#
#   Allows the building with (or without) the custom unknown command
#
# Arguments:
#   none
#   
# Results:
#
#   Adds the following arguments to configure:
#       --enable-unknown=yes|no
#
#   Defines the following vars:
#
#   Sets the following vars:
#
#------------------------------------------------------------------------

AC_DEFUN(TDOM_ENABLE_UNKNOWN, [
    AC_MSG_CHECKING([wether to enable built-in unknown command])
    AC_ARG_ENABLE(ucmd,
    [  --enable-unknown        enable built-in unknown command [--disable-unknown]],
    [tcl_ok=$enableval], [tcl_ok=no])

    if test "${enable_unknown+set}" = set; then
        enableval="$enable_unknown"
        tcl_ok=$enableval
    else
        tcl_ok=no
    fi

    if test "$tcl_ok" = "no" ; then
        AC_MSG_RESULT([no])
        AC_DEFINE(TDOM_NO_UNKNOWN_CMD)
    else
        AC_MSG_RESULT([yes])
    fi
])
#------------------------------------------------------------------------
# TDOM_ENABLE_TDOMALLOC --
#
#   Allows the building with tDOMs block allocator for nodes
#
# Arguments:
#   none
#
# Results:
#
#   Adds the following arguments to configure:
#       --enable-tdomalloc=yes|no
#
#   Defines the following vars:
#
#   Sets the following vars:
#
#------------------------------------------------------------------------

AC_DEFUN(TDOM_ENABLE_TDOMALLOC, [
    AC_MSG_CHECKING([wether to enable tDOMs block allocator])
    AC_ARG_ENABLE(tdomalloc,
    [  --enable-tdomalloc      build with the tDOM allocator [--enable-tdomalloc]],
    [tcl_ok=$enableval], [tcl_ok=yes])

    if test "${enable_tdomalloc+set}" = set; then
        enableval="$enable_tdomalloc"
        tcl_ok=$enableval
    else
        tcl_ok=yes
    fi

    if test "$tcl_ok" = "yes" ; then
        AC_MSG_RESULT([yes])
    else
        AC_MSG_RESULT([no])
        AC_DEFINE(USE_NORMAL_ALLOCATOR)
    fi
])

#------------------------------------------------------------------------
# TDOM_PATH_AOLSERVER
#
#   Allows the building with support for AOLserver 
#
# Arguments:
#   none
#   
# Results:
#
#   Adds the following arguments to configure:
#       --with-aolserver=...
#
#   Defines the following vars:
#       AOL_DIR Full path to the directory containing AOLserver distro
#
#   Sets the following vars:
#       NS_AOLSERVER 
#------------------------------------------------------------------------

AC_DEFUN(TDOM_PATH_AOLSERVER, [
    AC_ARG_WITH(aol, [  --with-aolserver        directory containing AOLserver distribution], with_aolserver=${withval})
    AC_MSG_CHECKING([for AOLserver configuration])
    AC_CACHE_VAL(ac_cv_c_aolserver,[

    if test x"${with_aolserver}" != x ; then
        if test -f "${with_aolserver}/include/ns.h" ; then
            ac_cv_c_aolserver=`(cd ${with_aolserver}; pwd)`
        else
            AC_MSG_ERROR([${with_aolserver} directory doesn't contain ns.h])
        fi
    fi
    ])

    if test x"${ac_cv_c_aolserver}" = x ; then
        AC_MSG_RESULT([none found])
    else
        AOL_DIR=${ac_cv_c_aolserver}
        AC_MSG_RESULT([found AOLserver in $AOL_DIR])
        AC_DEFINE(NS_AOLSERVER)
        AC_DEFINE(USE_NORMAL_ALLOCATOR)
    fi
])
