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
// 핸들, 핸들의 타입, 에러 코드를 넘겨줌
void disp_error(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
    SQLSMALLINT iRec = 0;
    SQLINTEGER iError;
    WCHAR wszMessage[1000];
    WCHAR wszState[SQL_SQLSTATE_SIZE + 1];
    if (RetCode == SQL_INVALID_HANDLE) { // 핸들이 없는 경우
        fwprintf(stderr, L"Invalid handle!\n");
        return;
    }
    // 준 타입의 준 핸들에 대한 에러를 문자열로 출력
    // 에러가 여러개일 수 있어서 while 루프를 돌면서 출력하는 것임
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
    SQLSMALLINT dLevel; // db랑 같은 타입으로 받아라
    SQLLEN cbName = 0, cbLevel = 0, cbId = 0; // 실제 테이블을 읽었을때 몇칸이냐? 뭔말임이게
    // DB의 몇자리 들어가 있는가??..

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

                    // 어떻게 읽는지 자세히 살펴봅시다
                    retcode = SQLExecDirect(hstmt, (SQLWCHAR*)L"SELECT user_id, user_name, user_level FROM user_table", SQL_NTS); // 쿼리는 핵심만 간단하게
                        
                    // SELECT: 필요한것을 읽는 것. column 이름을 적어서 읽으면 됨. SQLExecDirect 함수로 실행.
                    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

                        // Bind columns 1, 2, and 3 
                        // 각각의 column 하나하나에 맞는 변수를 지정해주어야 함.
                        // 숫자를 집어넣음(몇번째 데이터를 연결할 것인지)
                        // 자료형을 맞춰서 받아야 함.
                        // 타입 맞춰주는게 중요. 똑같은 크기의 자료형으로 맞춰주는게 까다로움.
                        retcode = SQLBindCol(hstmt, 1, SQL_C_LONG, &dId, 10, &cbId); // 최대 10자리(10억?)
                        retcode = SQLBindCol(hstmt, 2, SQL_C_WCHAR, szName, 20, &cbName); // 최대길이 20
                        retcode = SQLBindCol(hstmt, 3, SQL_C_SHORT, &dLevel, 10, &cbLevel);

                        // Fetch and print each row of data. On an error, display a message and exit.  
                        for (int i = 0; ; i++) { // 데이터가 몇개 날라올지 몰라서 무한반복하는듯
                            retcode = SQLFetch(hstmt); // SQLFetch: 데이터를 하나하나씩 읽는것(SQLExecDirect의 결과를 가져오기)
                            // odbc에 내장된 버퍼에 읽어옴. 
                            // fetch의 결과가 error도 success도 아니면 데이터를 다 읽은 것임

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