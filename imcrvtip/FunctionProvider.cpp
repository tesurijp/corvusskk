﻿
#include "imcrvtip.h"
#include "TextService.h"
#include "EditSession.h"
#include "FnCandidateList.h"

STDAPI CTextService::GetType(GUID *pguid)
{
	if (pguid == nullptr)
	{
		return E_INVALIDARG;
	}

	*pguid = c_clsidTextService;

	return S_OK;
}

STDAPI CTextService::GetDescription(BSTR *pbstrDesc)
{
	BSTR bstrDesc = nullptr;

	if (pbstrDesc == nullptr)
	{
		return E_INVALIDARG;
	}

	*pbstrDesc = nullptr;

	bstrDesc = SysAllocString(TextServiceDesc);

	if (bstrDesc == nullptr)
	{
		return E_OUTOFMEMORY;
	}

	*pbstrDesc = bstrDesc;

	return S_OK;
}

STDAPI CTextService::GetFunction(REFGUID rguid, REFIID riid, IUnknown **ppunk)
{
	if (ppunk == nullptr)
	{
		return E_INVALIDARG;
	}

	*ppunk = nullptr;

	//This value can be GUID_NULL.
	if (!IsEqualGUID(rguid, GUID_NULL) && !IsEqualGUID(rguid, c_clsidTextService))
	{
		return E_INVALIDARG;
	}

	if (IsEqualIID(riid, IID_ITfFnConfigure))
	{
		*ppunk = static_cast<ITfFnConfigure *>(this);
	}
	else if (IsEqualIID(riid, IID_ITfFnShowHelp))
	{
		*ppunk = static_cast<ITfFnShowHelp *>(this);
	}
	else if (IsEqualIID(riid, IID_ITfFnReconversion))
	{
		*ppunk = static_cast<ITfFnReconversion *>(this);
	}
	else if (IsEqualIID(riid, IID_ITfFnGetPreferredTouchKeyboardLayout))
	{
		*ppunk = static_cast<ITfFnGetPreferredTouchKeyboardLayout *>(this);
	}

	if (*ppunk)
	{
		AddRef();
		return S_OK;
	}

	return E_NOINTERFACE;
}

STDAPI CTextService::GetDisplayName(BSTR *pbstrName)
{
	BSTR bstrName = nullptr;

	if (pbstrName == nullptr)
	{
		return E_INVALIDARG;
	}

	*pbstrName = nullptr;

	bstrName = SysAllocString(TextServiceDesc);

	if (bstrName == nullptr)
	{
		return E_OUTOFMEMORY;
	}

	*pbstrName = bstrName;

	return S_OK;
}

STDAPI CTextService::Show(HWND hwndParent, LANGID langid, REFGUID rguidProfile)
{
	if (!IsEqualGUID(rguidProfile, c_guidProfile))
	{
		return E_INVALIDARG;
	}

	_StartConfigure();

	return S_OK;
}

STDAPI CTextService::Show(HWND hwndParent)
{
	_StartConfigure();

	return S_OK;
}

STDAPI CTextService::QueryRange(ITfRange *pRange, ITfRange **ppNewRange, BOOL *pfConvertable)
{
	if (pRange == nullptr || pfConvertable == nullptr)
	{
		return E_INVALIDARG;
	}

	if (ppNewRange == nullptr)
	{
		*pfConvertable = TRUE;
		return S_OK;
	}

	*ppNewRange = nullptr;
	*pfConvertable = FALSE;

	HRESULT hr = pRange->Clone(ppNewRange);

	if (SUCCEEDED(hr))
	{
		*pfConvertable = TRUE;
	}

	return hr;
}

STDAPI CTextService::GetReconversion(ITfRange *pRange, ITfCandidateList **ppCandList)
{
	if (pRange == nullptr || ppCandList == nullptr)
	{
		return E_INVALIDARG;
	}

	*ppCandList = nullptr;

	HRESULT hr = E_FAIL;

	std::wstring text;
	if (SUCCEEDED((_GetRangeText(pRange, text))) && !text.empty())
	{
		std::wstring key;
		_ConvKanaToKana(text, im_katakana, key, im_hiragana);

		CComPtr<CTextService> pTextService;

		try
		{
			pTextService.Attach(new CTextService());

			pTextService->_ResetStatus();
			pTextService->_CreateConfigPath();
			pTextService->inputmode = im_hiragana;
			pTextService->kana = key;

			pTextService->_StartSubConv(REQ_SEARCH);

			*ppCandList = new CFnCandidateList(this, key, pTextService->candidates);

			hr = S_OK;
		}
		catch (...)
		{
			return E_OUTOFMEMORY;
		}
	}

	return hr;
}

STDAPI CTextService::Reconvert(ITfRange *pRange)
{
	if (pRange == nullptr)
	{
		return E_INVALIDARG;
	}

	if (_IsComposing())
	{
		return S_OK;
	}

	HRESULT hr = E_FAIL;

	std::wstring text;
	if (SUCCEEDED(_GetRangeText(pRange, text)) && !text.empty())
	{
		CComPtr<ITfDocumentMgr> pDocumentMgr;
		if (SUCCEEDED(_pThreadMgr->GetFocus(&pDocumentMgr)) && (pDocumentMgr != nullptr))
		{
			CComPtr<ITfContext> pContext;
			if (SUCCEEDED(pDocumentMgr->GetTop(&pContext)) && (pContext != nullptr))
			{
				reconversion = TRUE;
				reconvsrc = text;

				if (!_IsKeyboardOpen())
				{
					inputmode = im_disable;
					_SetKeyboardOpen(TRUE);
				}

				inputmode = im_hiragana;
				inputkey = TRUE;
				_ConvKanaToKana(text, im_katakana, kana, im_hiragana);

				hr = _InvokeKeyHandler(pContext, 0, 0, SKK_NEXT_CAND);
			}
		}
	}

	return hr;
}

STDAPI CTextService::GetLayout(TKBLayoutType *pTKBLayoutType, WORD *pwPreferredLayoutId)
{
	if (pTKBLayoutType == nullptr || pwPreferredLayoutId == nullptr)
	{
		return E_INVALIDARG;
	}

	*pTKBLayoutType = TKBLT_OPTIMIZED;
	*pwPreferredLayoutId = TKBL_OPT_JAPANESE_ABC;

	return S_OK;
}


class CGetRangeTextEditSession : public CEditSessionBase
{
public:
	CGetRangeTextEditSession(CTextService *pTextService, ITfContext *pContext, ITfRange *pRange) : CEditSessionBase(pTextService, pContext)
	{
		_pRange = pRange;
	}

	~CGetRangeTextEditSession()
	{
		_pRange.Release();
	}

	// ITfEditSession
	STDMETHODIMP DoEditSession(TfEditCookie ec)
	{
		HRESULT hr;
		WCHAR buf[16];
		ULONG cch = _countof(buf) - 1;

		_Text.clear();

		while (cch == _countof(buf) - 1)
		{
			ZeroMemory(buf, sizeof(buf));
			cch = _countof(buf) - 1;
			hr = _pRange->GetText(ec, TF_TF_MOVESTART, buf, cch, &cch);
			if (SUCCEEDED(hr))
			{
				_Text.append(buf);
			}
			else
			{
				_Text.clear();
				return hr;
			}
		}

		return S_OK;
	}

	std::wstring _GetText()
	{
		return _Text;
	}

private:
	CComPtr<ITfRange> _pRange;
	std::wstring _Text;
};

HRESULT CTextService::_GetRangeText(ITfRange *pRange, std::wstring &text)
{
	HRESULT hr = E_FAIL;

	CComPtr<ITfContext> pContext;
	if (SUCCEEDED(pRange->GetContext(&pContext)) && (pContext != nullptr))
	{
		try
		{
			CComPtr<CGetRangeTextEditSession> pEditSession;
			pEditSession.Attach(
				new CGetRangeTextEditSession(this, pContext, pRange));
			pContext->RequestEditSession(_ClientId, pEditSession, TF_ES_SYNC | TF_ES_READ, &hr);
			if (SUCCEEDED(hr))
			{
				text = pEditSession->_GetText();
			}
		}
		catch (...)
		{
			return E_OUTOFMEMORY;
		}
	}

	return hr;
}

class CSetResultEditSession : public CEditSessionBase
{
public:
	CSetResultEditSession(CTextService *pTextService, ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
	{
	}

	~CSetResultEditSession()
	{
	}

	// ITfEditSession
	STDMETHODIMP DoEditSession(TfEditCookie ec)
	{
		return _pTextService->_HandleCharReturn(ec, _pContext);
	}
};

HRESULT CTextService::_SetResult(const std::wstring &fnsearchkey, const CANDIDATES &fncandidates, UINT index)
{
	HRESULT hr = E_FAIL;

	if (index >= (ULONG)fncandidates.size())
	{
		return E_FAIL;
	}

	CComPtr<ITfDocumentMgr> pDocumentMgr;
	if (SUCCEEDED(_pThreadMgr->GetFocus(&pDocumentMgr)) && (pDocumentMgr != nullptr))
	{
		CComPtr<ITfContext> pContext;
		if (SUCCEEDED(pDocumentMgr->GetTop(&pContext)) && (pContext != nullptr))
		{
			inputkey = TRUE;
			searchkey = fnsearchkey;
			searchkeyorg = fnsearchkey;
			showentry = TRUE;
			candidates = fncandidates;
			candidx = (size_t)index;
			if (candidx >= cx_untilcandlist - 1)
			{
				showcandlist = TRUE;
			}

			try
			{
				CComPtr<ITfEditSession> pEditSession;
				pEditSession.Attach(
					new CSetResultEditSession(this, pContext));
				pContext->RequestEditSession(_ClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
			}
			catch (...)
			{
				hr = E_OUTOFMEMORY;
			}
		}
	}

	return hr;
}

BOOL CTextService::_InitFunctionProvider()
{
	HRESULT hr = E_FAIL;

	CComPtr<ITfSourceSingle> pSourceSingle;
	if (SUCCEEDED(_pThreadMgr->QueryInterface(IID_PPV_ARGS(&pSourceSingle))) && (pSourceSingle != nullptr))
	{
		hr = pSourceSingle->AdviseSingleSink(_ClientId, IID_IUNK_ARGS(static_cast<ITfFunctionProvider *>(this)));
	}

	return SUCCEEDED(hr);
}

void CTextService::_UninitFunctionProvider()
{
	CComPtr<ITfSourceSingle> pSourceSingle;
	if (SUCCEEDED(_pThreadMgr->QueryInterface(IID_PPV_ARGS(&pSourceSingle))) && (pSourceSingle != nullptr))
	{
		pSourceSingle->UnadviseSingleSink(_ClientId, IID_ITfFunctionProvider);
	}
}
