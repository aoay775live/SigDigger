//
//    AudioConfig.h: Audio device configuration
//    Copyright (C) 2023 Gonzalo José Carracedo Carballal
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as
//    published by the Free Software Foundation, either version 3 of the
//    License, or (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful, but
//    WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public
//    License along with this program.  If not, see
//    <http://www.gnu.org/licenses/>
//

#ifndef AUDIOCONFIG_H
#define AUDIOCONFIG_H

#include <Suscan/Serializable.h>

#include <QObject>

namespace SigDigger {
  class AudioConfig : public Suscan::Serializable {
    std::string devStr;
    std::string description;

  public:
    AudioConfig();
    AudioConfig(Suscan::Object const &);

    // Overriden methods
    void loadDefaults();
    void deserialize(Suscan::Object const &conf) override;
    Suscan::Object &&serialize() override;
  };
}
#endif // AUDIOCONFIG_H
