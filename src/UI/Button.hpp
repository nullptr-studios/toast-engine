/**
 * @file Button.hpp
 * @author Dante Harper
 * @date 19/01/26
 */

#pragma once

#include "Toast/Objects/Actor.hpp"

class Button : public toast::Actor {
  public:
  REGISTER_TYPE(Button);

  void Init() override {}

};
