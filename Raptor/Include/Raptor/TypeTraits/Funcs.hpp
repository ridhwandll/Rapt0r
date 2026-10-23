/*
  +------------------------------------------------+
  |  _____             _    ___  _____    01010010 |
  | │  __ ╲           │ │  ╱ _ ╲│  __ ╲   01100001 |
  | │ │__) │__ _ _ __ │ │_│ │ │ │ │__) │  01110000 |
  | │  _  ╱╱ _` │ '_ ╲│ __│ │ │ │  _  ╱   01110100 |
  | │ │ ╲ ╲ (_│ │ │_) │ │_│ │_│ │ │ ╲ ╲   00110000 |
  | │_│  ╲_╲__,_│ .__╱ ╲__│╲___╱│_│  ╲_╲  01010010 |
  |             │ │ A fast and simple C++ 00010100 |
  |             │_│ binary serialization library   |
  +------------------------------------------------+
*/

#pragma once
#include <vector>
#include <type_traits>

#define RAPTOR_SERIALIZE_FUNC Rapt0RSerialize
#define RAPTOR_DESERIALIZE_FUNC Rapt0RDeserialize

namespace Rapt0R
{
    // Reader
    template <typename T, typename ReaderType>
    concept HasMemberDeserialize = requires(T val, ReaderType& ar)
    {
        { val.RAPTOR_DESERIALIZE_FUNC(ar) };
    };

    template <typename T, typename ReaderType>
    concept HasFreeDeserialize = requires(T val, ReaderType& ar)
    {
        { RAPTOR_DESERIALIZE_FUNC(ar, val) };
    };

    // Writer
    template <typename T, typename WriterType>
    concept HasMemberSerialize = requires(const T& val, WriterType& ar)
    {
        { val.RAPTOR_SERIALIZE_FUNC(ar) };
    };

    template <typename T, typename WriterType>
    concept HasFreeSerialize = requires(const T& val, WriterType& ar)
    {
        { RAPTOR_SERIALIZE_FUNC(ar, val) };
    };
}