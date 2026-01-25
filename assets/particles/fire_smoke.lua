-- Particle System Configuration
-- Fire with smoke effect

return {
    format = "particle_system",
    
    emitters = {
        -- Fire emitter
        {
            name = "Fire",
            enabled = true,
            
            -- Emission
            emissionMode = "continuous",
            emissionRate = 50,
            
            -- Shape
            shape = "cone",
            shapeSize = {0.3, 0.3, 0.3},
            coneAngle = 15,
            localOffset = {0, 0, 0},
            
            -- Particle life
            lifetime = {0.5, 1.5},
            
            -- Velocity
            speed = {2, 4},
            direction = {0, 1, 0},
            directionRandomness = 0.1,
            
            -- Size
            startSize = {0.2, 0.4},
            endSize = {0.05, 0.1},
            
            -- Rotation
            startRotation = {0, 360},
            rotationSpeed = {-30, 30},
            
            -- Color (orange/yellow fire)
            startColor = {1.0, 0.8, 0.2, 1.0},
            endColor = {1.0, 0.2, 0.0, 0.0},
            randomizeStartColor = false,
            
            -- Physics
            gravity = {0, 1.0, 0},
            drag = 0.2,
            
            -- Rendering
            texturePath = "placeholder",
            useTexture = true,
            additiveBlending = true,
            
            maxParticles = 5000
        },
        
        -- Smoke emitter
        {
            name = "Smoke",
            enabled = true,
            
            -- Emission
            emissionMode = "continuous",
            emissionRate = 15,
            
            -- Shape
            shape = "sphere",
            shapeSize = {0.3, 0.3, 0.3},
            localOffset = {0, 1.5, 0},  -- Offset above fire
            
            -- Particle life
            lifetime = {2.0, 4.0},
            
            -- Velocity
            speed = {0.5, 1.5},
            direction = {0, 1, 0},
            directionRandomness = 0.3,
            
            -- Size (smoke expands)
            startSize = {0.3, 0.5},
            endSize = {1.0, 2.0},
            
            -- Rotation
            startRotation = {0, 360},
            rotationSpeed = {-20, 20},
            
            -- Color (gray smoke fading out)
            startColor = {0.4, 0.4, 0.4, 0.6},
            endColor = {0.2, 0.2, 0.2, 0.0},
            randomizeStartColor = false,
            
            -- Physics
            gravity = {0, 0.2, 0},
            drag = 0.5,
            
            -- Rendering
            texturePath = "placeholder",
            useTexture = true,
            additiveBlending = false,
            
            maxParticles = 3000
        },
        
        -- Sparks emitter
        {
            name = "Sparks",
            enabled = true,
            
            -- Emission
            emissionMode = "burst",
            bursts = {
                { time = 0, count = 10, cycleInterval = 0.5 },
            },
            
            -- Shape
            shape = "point",
            localOffset = {0, 0.5, 0},
            
            -- Particle life
            lifetime = {0.3, 0.8},
            
            -- Velocity
            speed = {3, 6},
            direction = {0, 1, 0},
            directionRandomness = 0.8,
            
            -- Size (tiny sparks)
            startSize = {0.03, 0.06},
            endSize = {0.01, 0.02},
            
            -- Rotation
            startRotation = {0, 360},
            rotationSpeed = {0, 0},
            
            -- Color
            startColor = {1.0, 0.9, 0.5, 1.0},
            endColor = {1.0, 0.5, 0.0, 0.0},
            
            -- Physics
            gravity = {0, -8, 0},
            drag = 0.0,
            
            -- Rendering
            useTexture = false,
            additiveBlending = true,
            
            maxParticles = 500
        }
    }
}
