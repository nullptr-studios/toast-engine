# Input Layout (.til) {#file_input_layout}

A `.til` file represents an Input Layout format. It stores all the different
actions and binds a specific action has. It is encoded on plain `.json` and to
be used by the engine it should live on the `Layout folder` set on the project
settings.

The file will start with a key telling the format, in this case: `inputLayout`.
This is used for verifying the structure of the file is correct.

```json
"format" : "inputLayout",
```

After that, we will store the name of the current input layout. This name is
really important since it's what will be used by the end-user to switch between
different layouts.

```json
"name" : "gameplay",
```

Finally, we will start defining every action in an array:

```jsonc
"actions" : [
  // { your actions will go here }
]
```

## Actions

An action is something that the player may want to do, like jumping, walking,
opening the pause menu...

An action contains a name, a type and a bind array:

```json
{
  "name" : "jump",
  "type" : "button",
  "binds" : []
}
```

The name will be the one used in the code to subscribe the callbacks.

The type could be one of this three:

- **Button** (`button`): Refers to an action that can be pressed or released,
this includes keys, controller buttons and triggers.
- **1D Axis** (`axis1D`): An action that has a one dimensional value, like
a scroll wheel or a 1D composite.
- **2D Axis** (`axis2D`): An action that has a two dimensional value, like
a mouse position, a joystick or a 2D composite

After that, we will provide the binds for the action

## Binds

A bind is the physical mapping of that action onto a device and has the
following format:

```json
{
  "type" : "keyboard",
  "input" : 65
}
```

These are all the possible binds and what input they need to have:

- **Keyboard** (`keyboard`): Any key on the keyboard, the input is an int with
the [GLFW key code](https://www.glfw.org/docs/3.3/group__keys.html) (most of the
times, this will be the same as their ASCII code)
- **Cursor** (`mousePosition`): A Vector that returns the mouse position, this
action doesn't need any input data
- **Mouse Button** (`mouseButton`): Any button on the mouse (including scroll),
the input is an int with the code for the button: common ones are (0) left, (1)
right and (2) middle, the rest are often device specific
- **Mouse Scroll** (`mouseScroll`): Scrolling up or down the wheel, the input
is 0 for x-axis and 1 for y-axis
- **Controller Button** (`controllerButton`): A button or digital trigger on the
controller. The input are the
[GLFW codes](https://www.glfw.org/docs/3.3/group__gamepad__buttons.html)
for controller
- **Controller Trigger** (`controllerTrigger`): An analog trigger, the input is
(0) left trigger or (1) right trigger
- **Controller Stick** (`controllerStick`): A 2D stick on the controller, the
input can be (0) left stick or (1) right stick
- **1D Keyboard Composite** (`composite1D`): Binds two keyboard keys to left
and right and returns a float, the input is an array of two keyboard codes
ordered left, right
- **2D Keyboard Composite** (`composite2D`): Binds four keyboard keys to left,
right, top, and down and returns a 2D vector, the input is an array of four
keyboard codes ordered left, right, top, bottom

## Example

```json
{
    "format" : "inputLayout",
    "name" : "gameplay",
    "actions" : [
        {
            "name" : "move",
            "type" : "axis2D",
            "binds" : [
                {
                    "type" : "contrllerStick",
                    "input" : 0
                },
                {
                    "type" : "composite2D",
                    "input" : [65, 68, 87, 83]
                }
            ]
        },
        {
            "name" : "camera",
            "type" : "axis2D",
            "binds" : [
                {
                    "type" : "contrllerStick",
                    "input" : 1
                },
                {
                    "type" : "mousePosition",
                    "input" : ""
                }
            ]
        },
        {
            "name" : "jump",
            "type" : "button",
            "binds" : [
                {
                    "type" : "controllerButton",
                    "input" : 0
                },
                {
                    "type" : "keyboard",
                    "input" : 32
                }
            ]
        },
        {
            "name" : "shoot",
            "type" : "button",
            "binds" : [
                {
                    "type" : "controllerTrigger",
                    "input" : 1
                },
                {
                    "type" : "mouseButton",
                    "input" : 0
                }
            ]
        },
        {
            "name" : "change-weapon",
            "type" : "axis1D",
            "binds" : [
                {
                    "type" : "mouseScroll",
                    "input" : 1
                },
                {
                    "type" : "composite1D",
                    "input" : [266, 267]
                }
            ]
        }
    ]
}
```
