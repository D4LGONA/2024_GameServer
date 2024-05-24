// SQLBindCol_ref.cpp  
// compile with: odbc32.lib  
#include <windows.h>  
#include <stdio.h> 
#include <locale.h>

#define UNICODE  
#include <sqlext.h>  

#define NAME_LEN 50  
#define PHONE_LEN 60

/************************************************************************
/* HandleDiagnosticRecord : display error/warning information
/*
/* Parameters:
/* hHandle ODBC handle
/* hType Type of handle (SQL_HANDLE_STMT, SQL_HANDLE_ENV, SQL_HANDLE_DBC)
/* RetCode Return code of failing command
/************************************************************************/
// �ڵ�, �ڵ��� Ÿ��, ���� �ڵ带 �Ѱ���
void disp_error(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
    SQLSMALLINT iRec = 0;
    SQLINTEGER iError;
    WCHAR wszMessage[1000];
    WCHAR wszState[SQL_SQLSTATE_SIZE + 1];
    if (RetCode == SQL_INVALID_HANDLE) { // �ڵ��� ���� ���
        fwprintf(stderr, L"Invalid handle!\n");
        return;
    }
    // �� Ÿ���� �� �ڵ鿡 ���� ������ ���ڿ��� ���
    // ������ �������� �� �־ while ������ ���鼭 ����ϴ� ����
    while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
        (SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
        // Hide data truncated..
        if (wcsncmp(wszState, L"01004", 5)) {
            fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
        }
    }
}


void show_error() {
    printf("error\n");
}

int main() {
    SQLHENV henv;
    SQLHDBC hdbc;
    SQLHSTMT hstmt = 0;
    SQLRETURN retcode;
    SQLWCHAR szName[NAME_LEN];
    SQLINTEGER dId;
    SQLSMALLINT dLevel; // db�� ���� Ÿ������ �޾ƶ�
    SQLLEN cbName = 0, cbLevel = 0, cbId = 0; // ���� ���̺��� �о����� ��ĭ�̳�? �������̰�
    // DB�� ���ڸ� �� �ִ°�??..

    setlocale(LC_ALL, "korean");
    // Allocate environment handle  
    retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

    // Set the ODBC version environment attribute  
    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
        retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

        // Allocate connection handle  
        if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
            retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

            // Set login timeout to 5 seconds  
            if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

                // Connect to data source  
                retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2024_TF", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

                // Allocate statement handle  
                if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                    retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

                    // ��� �д��� �ڼ��� ���캾�ô�
                    retcode = SQLExecDirect(hstmt, (SQLWCHAR*)L"SELECT user_id, user_name, user_level FROM user_table", SQL_NTS); // ������ �ٽɸ� �����ϰ�
                        
                    // SELECT: �ʿ��Ѱ��� �д� ��. column �̸��� ��� ������ ��. SQLExecDirect �Լ��� ����.
                    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

                        // Bind columns 1, 2, and 3 
                        // ������ column �ϳ��ϳ��� �´� ������ �������־�� ��.
                        // ���ڸ� �������(���° �����͸� ������ ������)
                        // �ڷ����� ���缭 �޾ƾ� ��.
                        // Ÿ�� �����ִ°� �߿�. �Ȱ��� ũ���� �ڷ������� �����ִ°� ��ٷο�.
                        retcode = SQLBindCol(hstmt, 1, SQL_C_LONG, &dId, 10, &cbId); // �ִ� 10�ڸ�(10��?)
                        retcode = SQLBindCol(hstmt, 2, SQL_C_WCHAR, szName, 20, &cbName); // �ִ���� 20
                        retcode = SQLBindCol(hstmt, 3, SQL_C_SHORT, &dLevel, 10, &cbLevel);

                        // Fetch and print each row of data. On an error, display a message and exit.  
                        for (int i = 0; ; i++) { // �����Ͱ� � ������� ���� ���ѹݺ��ϴµ�
                            retcode = SQLFetch(hstmt); // SQLFetch: �����͸� �ϳ��ϳ��� �д°�(SQLExecDirect�� ����� ��������)
                            // odbc�� ����� ���ۿ� �о��. 
                            // fetch�� ����� error�� success�� �ƴϸ� �����͸� �� ���� ����

                            if (retcode == SQL_ERROR)
                                show_error();
                            if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
                            {
                                //replace wprintf with printf
                                //%S with %ls
                                //warning C4477: 'wprintf' : format string '%S' requires an argument of type 'char *'
                                //but variadic argument 2 has type 'SQLWCHAR *'
                                //wprintf(L"%d: %S %S %S\n", i + 1, sCustID, szName, szPhone);  
                                wprintf(L"%d: %6d %20s %3d\n", i + 1, dId, szName, dLevel);
                            }
                            else
                                break;
                        }
                    }
                    else
                        disp_error(hstmt, SQL_HANDLE_STMT, retcode);

                    // Process data  
                    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                        SQLCancel(hstmt);
                        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
                    }

                    SQLDisconnect(hdbc);
                }

                SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
            }
        }
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }
}