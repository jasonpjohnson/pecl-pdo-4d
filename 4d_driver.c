/*
  +----------------------------------------------------------------------+
  | PECL :: PDO_4D                                                       |
  +----------------------------------------------------------------------+
  | Copyright (c) 2009 The PHP Group                                     |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  |                                                                      |
  | Unless required by applicable law or agreed to in writing, software  |
  | distributed under the License is distributed on an "AS IS" BASIS,    |
  | WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or      |
  | implied. See the License for the specific language governing         |
  | permissions and limitations under the License.                       |
  +----------------------------------------------------------------------+
  | Contributed by: 4D <php@4d.fr>, http://www.4d.com                    |
  |                 Alter Way, http://www.alterway.fr                    |
  | Authors: Stephane Planquart <stephane.planquart@o4db.com>            |
  |          Alexandre Morgaut <php@4d.fr>                               |
  +----------------------------------------------------------------------+
*/

/* $ Id: $ */


//#if HAVE_PDO_4D
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "pdo/php_pdo.h"
#include "pdo/php_pdo_driver.h"
#include "php_pdo_4d.h"
#include <zend_exceptions.h>

#include "php_pdo_4d_int.h"

int _pdo_4d_error(pdo_dbh_t *dbh, pdo_stmt_t *stmt, const char *file, int line) /* {{{ */
{
	pdo_4d_db_handle *H = (pdo_4d_db_handle *)dbh->driver_data;
	pdo_error_type *pdo_err;
	pdo_4d_error_info *einfo;
	pdo_4d_stmt *S = NULL;

	if (stmt) {
		S = (pdo_4d_stmt*)stmt->driver_data;
		pdo_err = &stmt->error_code;
		einfo   = &S->einfo;
	} else {
		pdo_err = &dbh->error_code;
		einfo   = &H->einfo;
	}

	//pdo_err = &dbh->error_code;
	//einfo   = &H->einfo;
	einfo->errcode = fourd_errno(H->server);
	einfo->file = file;
	einfo->line = line;

	/* free memory of last error message */
	if (einfo->errmsg) {
		pefree(einfo->errmsg, dbh->is_persistent);
		einfo->errmsg = NULL;
	}
	strlcpy(*pdo_err, fourd_sqlstate(H->server), 6);
	/* if error_code is not null => get error message */
	/* printf("einfo->errcode:%d\n",einfo->errcode); */
	if (einfo->errcode) {
		/* printf("copy errmsg\n"); */
		einfo->errmsg = pestrdup(fourd_error(H->server), dbh->is_persistent);
	}
	else {
	  strlcpy(*pdo_err, PDO_ERR_NONE, 6);
		return 0;
	}
	if (!dbh->methods) {
		zend_throw_exception_ex(php_pdo_get_exception(), 0, "SQLSTATE[%s] [%d] %s",
				*pdo_err, einfo->errcode, einfo->errmsg);
	}

	return einfo->errcode;
}

static void pdo_4d_fetch_error_func(pdo_dbh_t *dbh, pdo_stmt_t *stmt, zval *info )
{
	pdo_4d_db_handle *H = (pdo_4d_db_handle *)dbh->driver_data;
	pdo_4d_error_info *einfo = &H->einfo;

	if (stmt) {
		pdo_4d_stmt *S = (pdo_4d_stmt*)stmt->driver_data;
		einfo = &S->einfo;
	} else {
		einfo = &H->einfo;
	}

	if (einfo->errcode) {
        add_next_index_long(info, einfo->errcode);
        add_next_index_string(info, einfo->errmsg);
	}

	return;
}

static void fourd_handle_closer(pdo_dbh_t *dbh ) /* {{{ */
{
  	if (!dbh || !dbh->driver_data) {
		return;
	}
	
	pdo_4d_db_handle *H = (pdo_4d_db_handle *)dbh->driver_data;

	if (H) {
		if (H->server) {
			fourd_close(H->server);
			fourd_free(H->server);
			H->server = NULL;
		}
		if (H->einfo.errmsg) {
			pefree(H->einfo.errmsg, dbh->is_persistent);
			H->einfo.errmsg = NULL;
		}
		pefree(H, dbh->is_persistent);
		dbh->driver_data = NULL;
	}
	return;
}

static long fourd_handle_doer(pdo_dbh_t *dbh, const zend_string *sql)
{
	pdo_4d_db_handle *H = (pdo_4d_db_handle *)dbh->driver_data;

	if (fourd_exec(H->server, ZSTR_VAL(sql))) {
		pdo_4d_error(dbh);
		return -1;
	} else {
		FOURD_LONG8 c = fourd_affected_rows(H->server);
		if (c == (FOURD_LONG8) -1) {
			pdo_4d_error(dbh);
			return (H->einfo.errcode ? -1 : 0);
		} else {
			return c;
		}
	}
}

static bool fourd_handle_preparer(pdo_dbh_t *dbh, zend_string *sql, pdo_stmt_t *stmt, zval *driver_options)
{
    pdo_4d_db_handle *H = (pdo_4d_db_handle *)dbh->driver_data;
    pdo_4d_stmt *S = ecalloc(1, sizeof(pdo_4d_stmt));
    zend_string *nsql = NULL;
    int ret;

    S->H = H;
    stmt->driver_data = S;
    stmt->methods = &fourd_stmt_methods;
    S->charset = H->charset;

    /* prepare statement */
    stmt->supports_placeholders = PDO_PLACEHOLDER_POSITIONAL;

    ret = pdo_parse_params(stmt, sql, &nsql);
    if (ret == 1) {
	  /* query was rewritten */
	  sql = nsql;
    } else if (ret == -1) {
        /* failed to parse */
        strcpy(dbh->error_code, dbh->error_code);
        return false;
    }
    if ((S->state = fourd_prepare_statement(H->server, ZSTR_VAL(sql))) == NULL) {
        if (nsql) {
		  zend_string_release(nsql);
        }
        pdo_4d_error(dbh);
        /*printf("Error sur prepare (%d):%s\n",fourd_errno(cnx),fourd_error(cnx));*/
    }
	
    if (nsql) {
        zend_string_release(nsql);
    }

    return true;
    /* end of prepare statement */
end:
    stmt->supports_placeholders = PDO_PLACEHOLDER_NONE;

    return 1;
}

static bool pdo_4d_set_attribute(pdo_dbh_t *dbh, zend_long attr, zval *val)
{
    pdo_4d_db_handle *H = (pdo_4d_db_handle *)dbh->driver_data;

    switch (attr) {
        case PDO_FOURD_ATTR_CHARSET:
            H->charset = pestrdup(Z_STRVAL_P(val), dbh->is_persistent);
            return true;

        case PDO_FOURD_ATTR_PREFERRED_IMAGE_TYPES:
            fourd_set_preferred_image_types(H->server, Z_STRVAL_P(val));
            return true;

        default:
            return false;
    }
}

static bool fourd_handle_begin(pdo_dbh_t *dbh )
{
	return 0 <= fourd_handle_doer(dbh, zend_string_init("BEGIN", sizeof("BEGIN")-1, 0));
}

static bool fourd_handle_commit(pdo_dbh_t *dbh )
{
	return 0 <= fourd_handle_doer(dbh, zend_string_init("COMMIT", sizeof("COMMIT")-1, 0));
}

static bool fourd_handle_rollback(pdo_dbh_t *dbh )
{
	return 0 <= fourd_handle_doer(dbh, zend_string_init("ROLLBACK", sizeof("ROLLBACK")-1, 0));
}

static int pdo_4d_get_attribute(pdo_dbh_t *dbh, long attr, zval *return_value)
{
    pdo_4d_db_handle *H = (pdo_4d_db_handle *)dbh->driver_data;

    switch (attr) {
        case PDO_FOURD_ATTR_CHARSET:
            ZVAL_STRING(return_value, H->charset);
            break;
        case PDO_FOURD_ATTR_PREFERRED_IMAGE_TYPES:
            ZVAL_STRING(return_value, fourd_get_preferred_image_types(H->server));
            break;
        default:
            return 0;
    }

    return 1;
}

static zend_string* fourd_handle_quoter(pdo_dbh_t *dbh, const zend_string *unquoted, enum pdo_param_type paramtype)
{
    int qcount = 0;
	char *c;
    zend_string *temp_str, *quoted;

    if (!ZSTR_LEN(unquoted)) {
        quoted = zend_string_init("''", 2, 0);
        return quoted;
    }

    /* count single quotes */
    for (c = (char *)ZSTR_VAL(unquoted); (c = strchr(c, '\'')); qcount++, c++)
        ; /* empty loop */

    int quotedlen = ZSTR_LEN(unquoted) + qcount + 2;
    quoted = zend_string_alloc(quotedlen, 0);

    c = ZSTR_VAL(quoted);
    *c++ = '\'';

    /* foreach (chunk that ends in a quote) */
    char *l = (char *)ZSTR_VAL(unquoted), *r;
    while ((r = strchr(l, '\''))) {
        temp_str = zend_string_init(l, r - l + 1, 0);
        memcpy(c, ZSTR_VAL(temp_str), ZSTR_LEN(temp_str));
        c += ZSTR_LEN(temp_str);
        *c++ = '\''; /* add second quote */
        zend_string_release(temp_str);
        l = r + 1;
    }

    /* Copy remainder and add enclosing quote */
    temp_str = zend_string_init(l, ZSTR_LEN(unquoted) - (l - ZSTR_VAL(unquoted)), 0);
    memcpy(c, ZSTR_VAL(temp_str), ZSTR_LEN(temp_str));
    zend_string_release(temp_str);
    ZSTR_VAL(quoted)[quotedlen - 1] = '\'';
    ZSTR_VAL(quoted)[quotedlen] = '\0';

    return quoted;
}

/* }}} */

static struct pdo_dbh_methods fourd_methods = {
		fourd_handle_closer,				//mysql_handle_closer,
		fourd_handle_preparer,				//mysql_handle_preparer,
		fourd_handle_doer,				//mysql_handle_doer,
		fourd_handle_quoter,				//mysql_handle_quoter,
		fourd_handle_begin,				//mysql_handle_begin,
		fourd_handle_commit,				//mysql_handle_commit,
		fourd_handle_rollback,				//mysql_handle_rollback,
		pdo_4d_set_attribute,				//pdo_mysql_set_attribute,
		NULL,				//pdo_mysql_last_insert_id,
		pdo_4d_fetch_error_func,				//pdo_mysql_fetch_error_func,
		pdo_4d_get_attribute,				//pdo_mysql_get_attribute,
		NULL				//pdo_mysql_check_liveness
};


static int pdo_4d_handle_factory(pdo_dbh_t *dbh, zval *driver_options )
{
	pdo_4d_db_handle *H;
	char *host = NULL;
	unsigned int port = 19812;
	char *dbname = NULL;
	char *charset = NULL;
	char *user=NULL;
	char *pwd=NULL;
	int timeout=0;
	char *preferred_image_types;
	struct pdo_data_src_parser vars[] = {
			{ "host",       "localhost",  0 },
			{ "port",   		"19812",			0 },
			{ "dbname",   	"",						0 },
			{ "charset",  	"UTF-16LE",	0 },
			{ "user",  	"",	0 },
			{ "password",  	"",	0 },
	};
	php_pdo_parse_data_source(dbh->data_source, dbh->data_source_len, vars,6);

	H=pecalloc(1, sizeof(pdo_4d_db_handle), dbh->is_persistent);

	H->einfo.errcode = 0;
	H->einfo.errmsg = NULL;
	if((H->server=fourd_init())==NULL)
	{
		pdo_4d_error(dbh);
		//think to code free method
		dbh->methods = &fourd_methods;
		return 0;
	}
	/*set timeout */
	timeout=INI_INT("pdo_4d.timeout");
	if(timeout<=0) {
		timeout=30;/*30 is timeout by default */
	}
	fourd_timeout(H->server,timeout);
	/*set prefered image types */
	preferred_image_types=INI_STR("pdo_4d.preferred_image_types");
	if(preferred_image_types!=NULL && strlen(preferred_image_types)){
		fourd_set_preferred_image_types(H->server,preferred_image_types);
	}

	dbh->driver_data = H;
	H->max_buffer_size = 1024*1024;
	H->buffered = H->emulate_prepare = 1;	//review this one
	/* get user password from php.ini if is null */
	if(dbh->username==NULL) {
		user=vars[4].optval;
	}else{
		user=dbh->username;
	}
	if(dbh->password==NULL){
		pwd=vars[5].optval;
	}else{
		pwd=dbh->password;
	}
	//parce connection	 attribut
	host = vars[0].optval;
	if(vars[1].optval) {
		port = atoi(vars[1].optval);
	}
	dbname = vars[2].optval;
	charset = vars[3].optval;
	H->charset=charset;
	//connection
	if (fourd_connect(H->server, host, user, pwd, dbname, port)) // Returns 1 for an error
	{
		pdo_4d_error(dbh);
		dbh->methods = &fourd_methods;
		return 0;
	}

	H->attached = 1;

	dbh->alloc_own_columns = 1;
	dbh->max_escaped_char_length = 2;
	
	dbh->methods = &fourd_methods;

	return 1;
}



pdo_driver_t pdo_4d_driver = {
		PDO_DRIVER_HEADER(4D),
		pdo_4d_handle_factory
};

//#endif /* HAVE_PDO_4D */
