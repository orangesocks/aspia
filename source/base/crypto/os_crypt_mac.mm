//
// Aspia Project
// Copyright (C) 2016-2024 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#include "base/crypto/os_crypt.h"

#include "base/logging.h"
#include "base/strings/unicode.h"

namespace base {

// static
bool OSCrypt::encryptString16(std::u16string_view plaintext, std::string* ciphertext)
{
    return encryptString(utf8FromUtf16(plaintext), ciphertext);
}

// static
bool OSCrypt::decryptString16(std::string_view ciphertext, std::u16string* plaintext)
{
    std::string utf8;

    if (!decryptString(ciphertext, &utf8))
        return false;

    *plaintext = utf16FromUtf8(utf8);
    return true;
}

// static
bool OSCrypt::encryptString(std::string_view plaintext, std::string* ciphertext)
{
    NOTIMPLEMENTED();
    ciphertext->assign(plaintext);
    return true;
}

// static
bool OSCrypt::decryptString(std::string_view ciphertext, std::string* plaintext)
{
    NOTIMPLEMENTED();
    plaintext->assign(ciphertext);
    return true;
}

} // namespace base
