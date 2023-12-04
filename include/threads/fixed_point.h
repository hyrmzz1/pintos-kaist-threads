#include <stdint.h>

/* 정수 n을 고정 소수점 형식으로 변환할 때 n에 F를 곱함(n * F) */
/* 반대로 고정 소수점 수를 정수로 다시 변환할 때는 F로 나눔(x / F) */
/* pintos와 같은 운영 체제에서 고정 소수점 연산을 사용하는 주된 이유는 대부분의 하드웨어에서 부동 소수점 연산이 지원되지 않거나 성능상의 이유로 부동 */
#define F (1 << 14)
#define INT_MAX ((1 << 31) - 1)
#define INT_MIN (-(1 << 31))

/* 고정 소수점 연산을 위한 함수들 */

int int_to_fp (int n)   /* 정수 n을 고정 소수점 표현으로 변환함 */
{
  return n * F;
}

int fp_to_int (int x)   /* 고정 소수점 수 x를 정수로 변환함 (내림)*/
{
  return x / F;
}

int fp_to_int_round (int x) /* 고정 소수점 수 x를 가장 가까운 정수로 반올림하여 변환함 */
{
  if (x >= 0) return (x + F / 2) / F;
  else return (x - F / 2) / F;
}

int add_fp (int x, int y)   /* 두 고정 소수점 수 x와 y를 더함 */
{
  return x + y;
}

int sub_fp (int x, int y)   /* 두 고정 소수점 수 x에서 y를 뺌 */
{
  return x - y;
}

int add_mixed (int x, int n)    /* 고정 소수점 수 x에 정수 n을 더함 */
{
  return x + n * F;
}

int sub_mixed (int x, int n)    /* 고정 소수점 수 x에서 정수 n을 뺌 */
{
  return x - n * F;
}

int mult_fp (int x, int y)  /* 두 고정 소수점 수 x와 y를 곱함 */
{
  return ((int64_t) x) * y / F; /* 단순히 곱하게 되면 결과가 64비트 범위에 달할 수 있음. 따라서 두 32비트 수를 곱한 결과를 저장하기 위해 64비트 정수 자료형(int64_t)을 사용함 */
}

int mult_mixed (int x, int n)   /* 고정 소수점 수 x에 정수 n을 곱함 */
{
  return x * n;
}

int div_fp (int x, int y)   /* 고정 소수점 수 x를 고정 소수점 수 y로 나눔 */
{
  return ((int64_t) x) * F / y; /* 나눗셈을 수행하기 전에 분자를 64비트 정수로 확장하여, 나눗셈이 수행되는 동안 충분한 정밀도를 유지할 수 있도록 함 */
}

int div_mixed (int x, int n)    /* 고정 소수점 수 x를 정수 n으로 나눔 */
{
  return x / n;
}