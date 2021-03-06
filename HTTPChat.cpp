#include "pch.h"
#include <iostream>

/* Macros */

#define INITIALIZE_HTTP_RESPONSE( resp, status, reason )    \
    do                                                      \
    {                                                       \
        RtlZeroMemory( (resp), sizeof(*(resp)) );           \
        (resp)->StatusCode = (status);                      \
        (resp)->pReason = (reason);                         \
        (resp)->ReasonLength = (USHORT) strlen(reason);     \
    } while (FALSE)

#define ADD_KNOWN_HEADER(Response, HeaderId, RawValue)               \
    do                                                               \
    {                                                                \
        (Response).Headers.KnownHeaders[(HeaderId)].pRawValue =      \
                                                          (RawValue);\
        (Response).Headers.KnownHeaders[(HeaderId)].RawValueLength = \
            (USHORT) strlen(RawValue);                               \
    } while(FALSE)

#define ALLOC_MEM(cb) HeapAlloc(GetProcessHeap(), 0, (cb))
#define FREE_MEM(ptr) HeapFree(GetProcessHeap(), 0, (ptr))


/* Utility Functions Prototypes */
DWORD DoReceiveRequests(IN HANDLE hReqQueue);

DWORD
SendHttpResponse(
	IN HANDLE        hReqQueue,
	IN PHTTP_REQUEST pRequest,
	IN USHORT        StatusCode,
	IN PSTR          pReason,
	IN PSTR          pEntity
);

int __cdecl wmain(int argc, wchar_t* argv[])
{
	ULONG ret = 0;
	HTTPAPI_VERSION HttpApiVersion = HTTPAPI_VERSION_1;
	HANDLE hReqQueue = NULL;
	int urlAdded = 0;

	if (argc < 2)
	{
		wprintf(L"%ws : <URL1> [URL2] ... \n", argv[0]);
		return -1;
	}

	/* Initialize HTTP Server APIs */
	ret = HttpInitialize(HttpApiVersion, HTTP_INITIALIZE_SERVER, NULL);
	if (ret != NO_ERROR)
	{
		wprintf(L"HttpInitialize failed with %lu\n", ret);
		return ret;
	}

	/* Creating a request queue handle */
	ret = HttpCreateHttpHandle(&hReqQueue, 0);
	if (ret != NO_ERROR)
	{
		wprintf(L"HttpCreateHttpHandle failed with %lu\n", ret);
		goto CleanUp;
	}

	/* Registering the URLs to Listen On */
	for (int i = 1; i < argc; i++)
	{
		wprintf(L"Listening for requests on the following url : %s\n", argv[i]);

		ret = HttpAddUrl(hReqQueue, argv[i], NULL);
		if (ret != NO_ERROR)
		{
			wprintf(L"HttpAddUrl failed with %lu \n", ret);
			goto CleanUp;
		}
		else
		{
			urlAdded++;
		}
	}

	DoReceiveRequests(hReqQueue);

CleanUp:

	for (int i = 1; i < urlAdded; i++)
	{
		HttpRemoveUrl(hReqQueue, argv[i]);
	}

	if (hReqQueue)
	{
		CloseHandle(hReqQueue);
	}

	HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);

	return ret;
}

DWORD DoReceiveRequests(
	IN HANDLE hReqQueue
)
{
	ULONG ReqBufLength = sizeof(HTTP_REQUEST) + 2048; // 2 KB buffer
	PCHAR pReqBuf = (PCHAR)ALLOC_MEM(ReqBufLength);
	PHTTP_REQUEST pReq;
	HTTP_REQUEST_ID reqId;
	ULONG res;
	DWORD bytesRead;

	if (pReqBuf == NULL)
	{
		return ERROR_NOT_ENOUGH_MEMORY;
	}

	pReq = (PHTTP_REQUEST)pReqBuf;

	HTTP_SET_NULL_ID(&reqId);

	for (;;)
	{
		RtlZeroMemory(pReq, ReqBufLength);
		res = HttpReceiveHttpRequest(hReqQueue, reqId, 0, pReq, ReqBufLength, &bytesRead, NULL);
		if (res == NO_ERROR)
		{
			switch (pReq->Verb)
			{

			case HttpVerbGET:
			{
				std::string htmlResponse = "\
					<!DOCTYPE html>                                                      \
					<html>																 \
					<head>																 \
					<title>Chat</title>												 \
					</head>															 \
					<body>																 \
					<h1>This is the chat server.Using HTTP Server API</h1>			 \
					<form action = \"\">													 \
					Enter your message : <input type = \"text\" name = \"msg\"><br>			 \
					<input type = \"submit\" value = \"Submit\">							 \
					</form>															 \
					</body>															 \
					</html>	\
					";

				wprintf(L"Got a GET request for %ws\n", pReq->CookedUrl.pFullUrl);

				if (pReq->CookedUrl.QueryStringLength)
				{
					wprintf(L"Got a QUERY : %ws\n", pReq->CookedUrl.pQueryString);
					if (wcsncmp(pReq->CookedUrl.pQueryString, L"?msg=", 5) == 0)
					{
						PCWSTR parameter = pReq->CookedUrl.pQueryString + strlen("?msg=");
						wprintf(L"Query Parameter : %ws\n", parameter);
					}
				}

				res = SendHttpResponse(hReqQueue, pReq, 200, (PSTR)"OK", (PSTR)htmlResponse.c_str());
			} break;

			default:
			{
				wprintf(L"Got an unknown request for %ws\n", pReq->CookedUrl.pFullUrl);
				res = SendHttpResponse(hReqQueue, pReq, 503, (PSTR)"Not Implemented", NULL);
			} break;

			}

			if (res != NO_ERROR)
			{
				break;
			}

			HTTP_SET_NULL_ID(&reqId); // Reset for next request
		}
		else if (res == ERROR_MORE_DATA)
		{
			reqId = pReq->RequestId;

			/* Free Old Buffer Allocate New Buffer */
			ReqBufLength = bytesRead;
			FREE_MEM(pReqBuf);
			pReqBuf = (PCHAR)ALLOC_MEM(ReqBufLength);
			if (pReqBuf == NULL)
			{
				res = ERROR_NOT_ENOUGH_MEMORY;
				break;
			}

			pReq = (PHTTP_REQUEST)pReqBuf;
		}
		else if (res == ERROR_CONNECTION_INVALID && !HTTP_IS_NULL_ID(&reqId))
		{
			HTTP_SET_NULL_ID(&reqId);
		}
		else
		{
			break;
		}
	}

	if (pReqBuf)
	{
		FREE_MEM(pReqBuf);
	}

	return res;
}

DWORD
SendHttpResponse(
	IN HANDLE        hReqQueue,
	IN PHTTP_REQUEST pRequest,
	IN USHORT        StatusCode,
	IN PSTR          pReason,
	IN PSTR          pEntity
)
{
	HTTP_RESPONSE response;
	HTTP_DATA_CHUNK dataChunk;
	DWORD res;
	DWORD bytesSend;


	INITIALIZE_HTTP_RESPONSE(&response, StatusCode, pReason);

	ADD_KNOWN_HEADER(response, HttpHeaderContentType, "text/html");

	if (pEntity)
	{
		dataChunk.DataChunkType = HttpDataChunkFromMemory;
		dataChunk.FromMemory.pBuffer = pEntity;
		dataChunk.FromMemory.BufferLength = (ULONG)strlen(pEntity);
		response.EntityChunkCount = 1;
		response.pEntityChunks = &dataChunk;
	}

	res = HttpSendHttpResponse(hReqQueue, pRequest->RequestId, 0, &response, NULL, &bytesSend, NULL, 0, NULL, NULL);
	if (res != NO_ERROR)
	{
		wprintf(L"HttpSendHttpResponse failed with %lu\n", res);
	}

	return res;
}

