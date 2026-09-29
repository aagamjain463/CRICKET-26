// Stand-in for Unreal's automation test macros, so the engine-only auction tests run headless (see CoreMinimal.h).

#pragma once

#include "CoreMinimal.h"
#include <iostream>
#include <sstream>

enum class EAutomationTestFlags : uint32 { EditorContext = 1, ProductFilter = 2 };
constexpr EAutomationTestFlags operator|(EAutomationTestFlags A, EAutomationTestFlags B) { return EAutomationTestFlags(uint32(A) | uint32(B)); }

class FAutomationTestBase
{
public:
	explicit FAutomationTestBase(const char* InName) : Name(InName) { Registry().push_back(this); }
	virtual ~FAutomationTestBase() {}
	virtual bool RunTest(const FString& Parameters) = 0;

	const char* Name;
	int32 Errors = 0;
	static bool bVerbose;

	static std::vector<FAutomationTestBase*>& Registry() { static std::vector<FAutomationTestBase*> R; return R; }

	void AddError(const FString& S) { ++Errors; std::cout << "    ERROR " << *S << "\n"; }
	void AddWarning(const FString& S) { std::cout << "    warning " << *S << "\n"; }
	void AddInfo(const FString& S) { if (bVerbose) std::cout << "    " << *S << "\n"; }
	bool TestTrue(const FString& What, bool bValue) { if (!bValue) AddError(FString("expected true: ") + What); return bValue; }
	bool TestFalse(const FString& What, bool bValue) { if (bValue) AddError(FString("expected false: ") + What); return !bValue; }
	template <typename T> bool TestNotNull(const FString& What, const T* P) { if (!P) AddError(FString("expected not null: ") + What); return P != nullptr; }
	template <typename A, typename B> bool TestEqual(const FString& What, const A& X, const B& Y)
	{
		if (X == Y) return true;
		AddError(FString("expected equal: ") + What + " (" + Show(X) + " vs " + Show(Y) + ")");
		return false;
	}
	template <typename A, typename B> bool TestNotEqual(const FString& What, const A& X, const B& Y)
	{
		if (!(X == Y)) return true;
		AddError(FString("expected different: ") + What + " (" + Show(X) + ")");
		return false;
	}

private:
	static FString Show(const FString& S) { return S; }
	static FString Show(const char* S) { return FString(S); }
	template <typename T> static FString Show(const T& X)
	{
		if constexpr (std::is_arithmetic<T>::value) { std::ostringstream O; O << +X; return FString(O.str()); }
		else if constexpr (std::is_enum<T>::value) { return FString::FromInt(int32(X)); }
		else return FString("?");
	}
};
inline bool FAutomationTestBase::bVerbose = false;

#define IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, PrettyName, Flags) \
	class Class : public FAutomationTestBase \
	{ \
	public: \
		Class() : FAutomationTestBase(PrettyName) {} \
		virtual bool RunTest(const FString& Parameters) override; \
	}; \
	static Class Class##Instance;
