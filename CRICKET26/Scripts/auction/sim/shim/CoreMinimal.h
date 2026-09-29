// A small stand-in for the parts of Unreal's Core that the auction engine uses (TArray, TSet, TMap, FString, FMath,
// FRandomStream...), so AuctionTypes/AuctionData/AuctionEngine/AuctionAI and their engine-only tests build and run
// headless with a plain C++ compiler: Scripts/auction/sim/run.sh. Not the engine: only what the auction touches, with
// the same semantics where the auction depends on them (FRandomStream and HashCombine match Unreal bit for bit, and
// FMath::Min/Max/Clamp take one type, as Unreal's do, so a mixed-type call fails here as it would in the editor).

#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using int8 = int8_t;
using uint8 = uint8_t;
using int16 = int16_t;
using uint16 = uint16_t;
using int32 = int32_t;
using uint32 = uint32_t;
using int64 = int64_t;
using uint64 = uint64_t;
using TCHAR = char;
using ANSICHAR = char;

#define TEXT(x) x
#define INDEX_NONE (-1)
#define MAX_int32 (int32(0x7fffffff))
#define UE_ARRAY_COUNT(a) (int32(sizeof(a) / sizeof((a)[0])))
#define check(x) assert(x)
#define checkf(x, ...) assert(x)
#define ensure(x) (x)
#define FORCEINLINE inline
#define PLATFORM_ANDROID 0
#define PLATFORM_IOS 0
#define WITH_DEV_AUTOMATION_TESTS 1
#define UE_LOG(...) ((void)0)

template <typename T> inline typename std::remove_reference<T>::type&& MoveTemp(T&& V) { return static_cast<typename std::remove_reference<T>::type&&>(V); }
template <typename T> using TFunction = std::function<T>;
template <typename T> using TFunctionRef = std::function<T>;
template <typename T> using TUniquePtr = std::unique_ptr<T>;
template <typename T, typename... A> TUniquePtr<T> MakeUnique(A&&... Args) { return std::make_unique<T>(std::forward<A>(Args)...); }

inline uint32 HashCombine(uint32 A, uint32 C)
{
	uint32 B = 0x9e3779b9;
	A += B;
	A -= B; A -= C; A ^= (C >> 13);
	B -= C; B -= A; B ^= (A << 8);
	C -= A; C -= B; C ^= (B >> 13);
	A -= B; A -= C; A ^= (C >> 12);
	B -= C; B -= A; B ^= (A << 16);
	C -= A; C -= B; C ^= (B >> 5);
	A -= B; A -= C; A ^= (C >> 3);
	B -= C; B -= A; B ^= (A << 10);
	C -= A; C -= B; C ^= (B >> 15);
	return C;
}
inline uint32 GetTypeHash(int32 V) { return uint32(V); }
inline uint32 GetTypeHash(uint32 V) { return V; }

// ---- FMath -------------------------------------------------------------------------------------------------------

struct FMath
{
	template <class T> static T Min(const T A, const T B) { return A < B ? A : B; }
	template <class T> static T Max(const T A, const T B) { return A > B ? A : B; }
	template <class T> static T Clamp(const T X, const T Lo, const T Hi) { return X < Lo ? Lo : X > Hi ? Hi : X; }
	template <class T> static T Abs(const T A) { return A < T(0) ? -A : A; }
	template <class T> static T Square(const T A) { return A * A; }
	template <class T> static T Sign(const T A) { return A > T(0) ? T(1) : A < T(0) ? T(-1) : T(0); }
	template <class T, class U> static T Lerp(const T& A, const T& B, const U& Alpha) { return T(A + Alpha * (B - A)); }
	static int32 RoundToInt(float F) { return int32(std::floor(F + 0.5f)); }
	static int32 RoundToInt(double F) { return int32(std::floor(F + 0.5)); }
	static int32 FloorToInt(float F) { return int32(std::floor(F)); }
	static int32 FloorToInt(double F) { return int32(std::floor(F)); }
	static int32 CeilToInt(float F) { return int32(std::ceil(F)); }
	static int32 CeilToInt(double F) { return int32(std::ceil(F)); }
	static int32 TruncToInt(float F) { return int32(F); }
	static float Pow(float A, float B) { return std::pow(A, B); }
	static float Exp(float A) { return std::exp(A); }
	static float Loge(float A) { return std::log(A); }
	static float Sqrt(float A) { return std::sqrt(A); }
	static float Sin(float A) { return std::sin(A); }
	static float Fmod(float A, float B) { return std::fmod(A, B); }
	static bool IsNearlyZero(float A, float Tol = 1e-8f) { return std::fabs(A) <= Tol; }
	static bool IsNearlyEqual(float A, float B, float Tol = 1e-8f) { return std::fabs(A - B) <= Tol; }
	static float SmoothStep(float A, float B, float X)
	{
		if (X < A) return 0.f;
		if (X >= B) return 1.f;
		const float F = (X - A) / (B - A);
		return F * F * (3.f - 2.f * F);
	}
};

// ---- FString -----------------------------------------------------------------------------------------------------

template <typename T> class TArray;

enum class ESearchCase { CaseSensitive, IgnoreCase };
enum class ESearchDir { FromStart, FromEnd };

class FString
{
public:
	std::string S;
	FString() {}
	FString(const char* C) : S(C ? C : "") {}
	FString(const std::string& In) : S(In) {}
	FString(int32 Count, const char* C) : S(C, size_t(Count)) {}

	const char* operator*() const { return S.c_str(); }
	int32 Len() const { return int32(S.size()); }
	bool IsEmpty() const { return S.empty(); }
	void Reset() { S.clear(); }
	void Empty() { S.clear(); }
	char operator[](int32 I) const { return S[size_t(I)]; }
	char& operator[](int32 I) { return S[size_t(I)]; }

	static FString Printf(const char* Fmt, ...)
	{
		char Buf[8192];
		va_list Args;
		va_start(Args, Fmt);
		vsnprintf(Buf, sizeof(Buf), Fmt, Args);
		va_end(Args);
		return FString(Buf);
	}
	static FString FromInt(int32 N) { return FString(std::to_string(N)); }
	static FString Join(const TArray<FString>& Parts, const char* Sep);

	FString& operator+=(const FString& O) { S += O.S; return *this; }
	FString& operator+=(const char* O) { S += O; return *this; }
	FString& operator+=(char C) { S += C; return *this; }
	FString& AppendChar(char C) { S += C; return *this; }
	FString& Append(const FString& O) { S += O.S; return *this; }
	friend FString operator+(const FString& A, const FString& B) { return FString(A.S + B.S); }
	friend FString operator+(const FString& A, const char* B) { return FString(A.S + B); }
	friend FString operator+(const char* A, const FString& B) { return FString(std::string(A) + B.S); }
	bool operator==(const FString& O) const { return Lower(S) == Lower(O.S); } // Unreal compares case-insensitively
	bool operator!=(const FString& O) const { return !(*this == O); }
	bool operator==(const char* O) const { return *this == FString(O); }
	bool operator!=(const char* O) const { return !(*this == FString(O)); }
	bool operator<(const FString& O) const { return Lower(S) < Lower(O.S); }
	bool Equals(const FString& O, ESearchCase C = ESearchCase::CaseSensitive) const { return C == ESearchCase::CaseSensitive ? S == O.S : *this == O; }

	FString Left(int32 N) const { return FString(S.substr(0, size_t(std::max(0, std::min(N, Len()))))); }
	FString LeftChop(int32 N) const { return Left(Len() - N); }
	FString Right(int32 N) const { N = std::max(0, std::min(N, Len())); return FString(S.substr(S.size() - size_t(N))); }
	FString RightChop(int32 N) const { N = std::max(0, std::min(N, Len())); return FString(S.substr(size_t(N))); }
	void RightChopInline(int32 N) { *this = RightChop(N); }
	FString Mid(int32 Start, int32 Count = 1 << 30) const
	{
		Start = std::max(0, std::min(Start, Len()));
		return FString(S.substr(size_t(Start), size_t(std::max(0, std::min(Count, Len() - Start)))));
	}
	bool StartsWith(const FString& P) const { return Lower(S).rfind(Lower(P.S), 0) == 0; }
	bool EndsWith(const FString& P) const { return Len() >= P.Len() && Lower(Right(P.Len()).S) == Lower(P.S); }
	bool Contains(const FString& P, ESearchCase C = ESearchCase::IgnoreCase) const
	{
		return C == ESearchCase::IgnoreCase ? Lower(S).find(Lower(P.S)) != std::string::npos : S.find(P.S) != std::string::npos;
	}
	int32 Find(const FString& P, ESearchCase C = ESearchCase::IgnoreCase, ESearchDir D = ESearchDir::FromStart, int32 From = INDEX_NONE) const
	{
		const std::string H = C == ESearchCase::IgnoreCase ? Lower(S) : S, N = C == ESearchCase::IgnoreCase ? Lower(P.S) : P.S;
		const size_t R = D == ESearchDir::FromStart ? H.find(N, From == INDEX_NONE ? 0 : size_t(From)) : H.rfind(N);
		return R == std::string::npos ? INDEX_NONE : int32(R);
	}
	bool FindChar(char C, int32& Index) const { const size_t R = S.find(C); Index = R == std::string::npos ? INDEX_NONE : int32(R); return R != std::string::npos; }
	bool Split(const FString& Delim, FString* L, FString* R) const
	{
		const size_t At = S.find(Delim.S);
		if (At == std::string::npos) return false;
		if (L) *L = FString(S.substr(0, At));
		if (R) *R = FString(S.substr(At + Delim.S.size()));
		return true;
	}
	FString Replace(const char* From, const char* To) const
	{
		std::string Out = S, F = From, T = To;
		if (F.empty()) return *this;
		for (size_t P = 0; (P = Out.find(F, P)) != std::string::npos; P += T.size()) Out.replace(P, F.size(), T);
		return FString(Out);
	}
	FString ToLower() const { return FString(Lower(S)); }
	FString ToUpper() const { std::string O = S; for (char& C : O) C = char(toupper(uint8(C))); return FString(O); }
	FString TrimStartAndEnd() const
	{
		size_t A = 0, B = S.size();
		while (A < B && isspace(uint8(S[A]))) ++A;
		while (B > A && isspace(uint8(S[B - 1]))) --B;
		return FString(S.substr(A, B - A));
	}
	bool IsNumeric() const
	{
		if (S.empty()) return false;
		char* End = nullptr;
		strtod(S.c_str(), &End);
		return End && *End == 0;
	}
	int32 ParseIntoArray(TArray<FString>& Out, const char* Delim, bool bCullEmpty = true) const;
	int32 ParseIntoArrayLines(TArray<FString>& Out, bool bCullEmpty = true) const;
	int32 ParseIntoArrayWS(TArray<FString>& Out) const;

	static std::string Lower(const std::string& In) { std::string O = In; for (char& C : O) C = char(tolower(uint8(C))); return O; }
};
inline uint32 GetTypeHash(const FString& V) { return uint32(std::hash<std::string>()(FString::Lower(V.S))); }

struct FCString
{
	static int32 Atoi(const char* C) { return int32(strtol(C, nullptr, 10)); }
	static int64 Atoi64(const char* C) { return int64(strtoll(C, nullptr, 10)); }
	static int32 Strtoi(const char* C, char** End, int32 Base) { return int32(strtol(C, End, Base)); }
	static float Atof(const char* C) { return float(strtod(C, nullptr)); }
};
struct FChar
{
	static bool IsAlnum(char C) { return isalnum(uint8(C)) != 0; }
	static bool IsWhitespace(char C) { return isspace(uint8(C)) != 0; }
	static bool IsDigit(char C) { return isdigit(uint8(C)) != 0; }
};

// ---- Containers --------------------------------------------------------------------------------------------------

template <typename T>
class TArray
{
public:
	std::vector<T> V;
	TArray() {}
	TArray(std::initializer_list<T> L) : V(L) {}
	TArray(const T* Ptr, int32 Count) : V(Ptr, Ptr + Count) {}

	int32 Num() const { return int32(V.size()); }
	bool IsEmpty() const { return V.empty(); }
	bool IsValidIndex(int32 I) const { return I >= 0 && I < Num(); }
	T& operator[](int32 I) { assert(IsValidIndex(I)); return V[size_t(I)]; }
	const T& operator[](int32 I) const { assert(IsValidIndex(I)); return V[size_t(I)]; }
	T& Last(int32 FromEnd = 0) { return (*this)[Num() - 1 - FromEnd]; }
	const T& Last(int32 FromEnd = 0) const { return (*this)[Num() - 1 - FromEnd]; }
	T* GetData() { return V.data(); }
	const T* GetData() const { return V.data(); }
	typename std::vector<T>::iterator begin() { return V.begin(); }
	typename std::vector<T>::iterator end() { return V.end(); }
	typename std::vector<T>::const_iterator begin() const { return V.begin(); }
	typename std::vector<T>::const_iterator end() const { return V.end(); }
	bool operator==(const TArray& O) const { return V == O.V; }
	bool operator!=(const TArray& O) const { return V != O.V; }

	int32 Add(const T& X) { V.push_back(X); return Num() - 1; }
	int32 Add(T&& X) { V.push_back(std::move(X)); return Num() - 1; }
	template <typename... A> int32 Emplace(A&&... Args) { V.emplace_back(std::forward<A>(Args)...); return Num() - 1; }
	int32 AddUnique(const T& X) { const int32 I = Find(X); return I != INDEX_NONE ? I : Add(X); }
	T& AddDefaulted_GetRef() { V.emplace_back(); return V.back(); }
	int32 AddDefaulted(int32 N = 1) { const int32 I = Num(); V.resize(V.size() + size_t(N)); return I; }
	int32 AddZeroed(int32 N = 1) { const int32 I = Num(); V.resize(V.size() + size_t(N), T()); return I; }
	void Append(const TArray& O) { V.insert(V.end(), O.V.begin(), O.V.end()); }
	void Append(std::initializer_list<T> L) { V.insert(V.end(), L.begin(), L.end()); }
	void Insert(const T& X, int32 At) { V.insert(V.begin() + At, X); }
	void RemoveAt(int32 I, int32 Count = 1) { V.erase(V.begin() + I, V.begin() + I + Count); }
	int32 Remove(const T& X) { const size_t N = V.size(); V.erase(std::remove(V.begin(), V.end(), X), V.end()); return int32(N - V.size()); }
	int32 RemoveSingle(const T& X) { const int32 I = Find(X); if (I == INDEX_NONE) return 0; RemoveAt(I); return 1; }
	template <typename P> int32 RemoveAll(P Pred) { const size_t N = V.size(); V.erase(std::remove_if(V.begin(), V.end(), Pred), V.end()); return int32(N - V.size()); }
	void Reset(int32 = 0) { V.clear(); }
	void Empty(int32 = 0) { V.clear(); }
	void Reserve(int32 N) { V.reserve(size_t(N)); }
	void SetNum(int32 N) { V.resize(size_t(N)); }
	void SetNumZeroed(int32 N) { V.resize(size_t(N), T()); }
	void Init(const T& X, int32 N) { V.assign(size_t(N), X); }
	void Swap(int32 A, int32 B) { std::swap(V[size_t(A)], V[size_t(B)]); }
	T Pop() { T X = V.back(); V.pop_back(); return X; }
	T& Top() { return V.back(); }

	int32 Find(const T& X) const { for (int32 I = 0; I < Num(); ++I) if (V[size_t(I)] == X) return I; return INDEX_NONE; }
	int32 IndexOfByKey(const T& X) const { return Find(X); }
	bool Contains(const T& X) const { return Find(X) != INDEX_NONE; }
	template <typename P> int32 IndexOfByPredicate(P Pred) const { for (int32 I = 0; I < Num(); ++I) if (Pred(V[size_t(I)])) return I; return INDEX_NONE; }
	template <typename P> bool ContainsByPredicate(P Pred) const { return IndexOfByPredicate(Pred) != INDEX_NONE; }
	template <typename P> T* FindByPredicate(P Pred) { const int32 I = IndexOfByPredicate(Pred); return I == INDEX_NONE ? nullptr : &V[size_t(I)]; }
	template <typename P> const T* FindByPredicate(P Pred) const { const int32 I = IndexOfByPredicate(Pred); return I == INDEX_NONE ? nullptr : &V[size_t(I)]; }
	template <typename P> TArray FilterByPredicate(P Pred) const { TArray O; for (const T& X : V) if (Pred(X)) O.Add(X); return O; }
	void Sort() { std::sort(V.begin(), V.end()); }
	template <typename P> void Sort(P Pred) { std::sort(V.begin(), V.end(), Pred); }
	template <typename P> void StableSort(P Pred) { std::stable_sort(V.begin(), V.end(), Pred); }
};

inline FString FString::Join(const TArray<FString>& Parts, const char* Sep)
{
	std::string O;
	for (int32 I = 0; I < Parts.Num(); ++I) { if (I) O += Sep; O += Parts[I].S; }
	return FString(O);
}
inline int32 FString::ParseIntoArray(TArray<FString>& Out, const char* Delim, bool bCullEmpty) const
{
	Out.Reset();
	const std::string D = Delim;
	size_t Start = 0;
	for (;;)
	{
		const size_t At = S.find(D, Start);
		const std::string Part = S.substr(Start, At == std::string::npos ? std::string::npos : At - Start);
		if (!bCullEmpty || !Part.empty()) Out.Add(FString(Part));
		if (At == std::string::npos) break;
		Start = At + D.size();
	}
	return Out.Num();
}
inline int32 FString::ParseIntoArrayLines(TArray<FString>& Out, bool bCullEmpty) const
{
	Out.Reset();
	std::string Cur;
	for (char C : S)
	{
		if (C == '\r') continue;
		if (C == '\n') { if (!bCullEmpty || !Cur.empty()) Out.Add(FString(Cur)); Cur.clear(); }
		else Cur += C;
	}
	if (!bCullEmpty || !Cur.empty()) Out.Add(FString(Cur));
	return Out.Num();
}
inline int32 FString::ParseIntoArrayWS(TArray<FString>& Out) const
{
	Out.Reset();
	std::string Cur;
	for (char C : S)
	{
		if (isspace(uint8(C))) { if (!Cur.empty()) Out.Add(FString(Cur)); Cur.clear(); }
		else Cur += C;
	}
	if (!Cur.empty()) Out.Add(FString(Cur));
	return Out.Num();
}

struct FShimHash
{
	template <typename K> size_t operator()(const K& Key) const { return size_t(GetTypeHash(Key)); }
};
struct FShimEq
{
	template <typename K> bool operator()(const K& A, const K& B) const { return A == B; }
};

template <typename T>
class TSet
{
public:
	std::unordered_set<T, FShimHash, FShimEq> V;
	TSet() {}
	TSet(std::initializer_list<T> L) { for (const T& X : L) V.insert(X); }
	void Add(const T& X) { V.insert(X); }
	bool Contains(const T& X) const { return V.count(X) != 0; }
	int32 Remove(const T& X) { return int32(V.erase(X)); }
	int32 Num() const { return int32(V.size()); }
	bool IsEmpty() const { return V.empty(); }
	void Reset() { V.clear(); }
	void Empty() { V.clear(); }
	TArray<T> Array() const { TArray<T> O; for (const T& X : V) O.Add(X); O.Sort(); return O; }
	typename std::unordered_set<T, FShimHash, FShimEq>::const_iterator begin() const { return V.begin(); }
	typename std::unordered_set<T, FShimHash, FShimEq>::const_iterator end() const { return V.end(); }
};

template <typename K, typename VT>
struct TPair
{
	K Key;
	VT Value;
};

// Ordered by key so iteration is deterministic (Unreal's TMap iterates in insertion order when nothing is removed;
// the auction never depends on the order).
template <typename K, typename VT>
class TMap
{
public:
	std::map<K, VT> M;
	VT& Add(const K& Key, const VT& Val) { return M[Key] = Val; }
	VT& FindOrAdd(const K& Key) { return M[Key]; }
	VT& FindOrAdd(const K& Key, const VT& Default) { auto It = M.find(Key); return It == M.end() ? (M[Key] = Default) : It->second; }
	bool Contains(const K& Key) const { return M.count(Key) != 0; }
	VT* Find(const K& Key) { auto It = M.find(Key); return It == M.end() ? nullptr : &It->second; }
	const VT* Find(const K& Key) const { auto It = M.find(Key); return It == M.end() ? nullptr : &It->second; }
	VT FindRef(const K& Key) const { auto It = M.find(Key); return It == M.end() ? VT() : It->second; }
	VT FindRef(const K& Key, const VT& Default) const { auto It = M.find(Key); return It == M.end() ? Default : It->second; }
	VT& operator[](const K& Key) { return M.at(Key); }
	const VT& operator[](const K& Key) const { return M.at(Key); }
	int32 Remove(const K& Key) { return int32(M.erase(Key)); }
	int32 Num() const { return int32(M.size()); }
	bool IsEmpty() const { return M.empty(); }
	void Reset() { M.clear(); }
	void Empty() { M.clear(); }

	struct FIter
	{
		typename std::map<K, VT>::iterator It;
		TPair<const K&, VT&> operator*() const { return { It->first, It->second }; }
		FIter& operator++() { ++It; return *this; }
		bool operator!=(const FIter& O) const { return It != O.It; }
	};
	struct FConstIter
	{
		typename std::map<K, VT>::const_iterator It;
		TPair<const K&, const VT&> operator*() const { return { It->first, It->second }; }
		FConstIter& operator++() { ++It; return *this; }
		bool operator!=(const FConstIter& O) const { return It != O.It; }
	};
	FIter begin() { return { M.begin() }; }
	FIter end() { return { M.end() }; }
	FConstIter begin() const { return { M.begin() }; }
	FConstIter end() const { return { M.end() }; }
};

// ---- Random ------------------------------------------------------------------------------------------------------

class FRandomStream
{
public:
	FRandomStream() : InitialSeed(0), Seed(0) {}
	FRandomStream(int32 In) { Initialize(In); }
	void Initialize(int32 In) { InitialSeed = In; Seed = uint32(In); }
	void Reset() { Seed = uint32(InitialSeed); }
	int32 GetInitialSeed() const { return InitialSeed; }
	int32 GetCurrentSeed() const { return int32(Seed); }
	float GetFraction() const
	{
		MutateSeed();
		const uint32 Bits = 0x3F800000U | (Seed >> 9);
		float Result;
		std::memcpy(&Result, &Bits, sizeof(Result));
		return Result - 1.0f;
	}
	float FRand() const { return GetFraction(); }
	int32 RandHelper(int32 A) const { return A > 0 ? FMath::Min(FMath::TruncToInt(GetFraction() * float(A)), A - 1) : 0; }
	int32 RandRange(int32 Min, int32 Max) const { const int32 Range = (Max - Min) + 1; return Min + RandHelper(Range); }
	float FRandRange(float Min, float Max) const { return Min + (Max - Min) * FRand(); }

private:
	void MutateSeed() const { Seed = (Seed * 196314165U) + 907633515U; }
	int32 InitialSeed;
	mutable uint32 Seed;
};

// ---- Colour ------------------------------------------------------------------------------------------------------

struct FColor
{
	uint8 R = 0, G = 0, B = 0, A = 255;
	FColor() {}
	FColor(uint8 InR, uint8 InG, uint8 InB, uint8 InA = 255) : R(InR), G(InG), B(InB), A(InA) {}
};
struct FLinearColor
{
	float R = 0.f, G = 0.f, B = 0.f, A = 1.f;
	FLinearColor() {}
	FLinearColor(float InR, float InG, float InB, float InA = 1.f) : R(InR), G(InG), B(InB), A(InA) {}
	static FLinearColor FromSRGBColor(const FColor& C)
	{
		auto Lin = [](uint8 V) { const float F = V / 255.f; return F <= 0.04045f ? F / 12.92f : std::pow((F + 0.055f) / 1.055f, 2.4f); };
		return FLinearColor(Lin(C.R), Lin(C.G), Lin(C.B), C.A / 255.f);
	}
	static const FLinearColor White;
};
inline const FLinearColor FLinearColor::White = FLinearColor(1.f, 1.f, 1.f, 1.f);
