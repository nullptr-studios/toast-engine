-- Particle System Configuration
-- Snow effect with wind

return {
    format = "particle_system",
    
    emitters = {
        {
            name = "Snowflakes",
            enabled = true,
            
            -- Emission
            emissionMode = "continuous",
            emissionRate = 50,
            
            -- Shape
            shape = "box",
            shapeSize = {20, 0.5, 20},
            localOffset = {0, 10, 0},
            
            -- Particle life
            lifetime = {4, 8},
            
            -- Velocity
            speed = {0.5, 1.0},
            direction = {0.1, -1, 0.05},  -- Slight wind
            directionRandomness = 0.1,
            
            -- Size
            startSize = {0.05, 0.15},
            endSize = {0.05, 0.15},
            
            -- Rotation
            startRotation = {0, 360},
            rotationSpeed = {-45, 45},
            
            -- Color
            startColor = {1.0, 1.0, 1.0, 0.9},
            endColor = {1.0, 1.0, 1.0, 0.0},
            
            -- Physics
            gravity = {0, -0.3, 0},
            drag = 0.3,
            
            -- Rendering
            texturePath = "placeholder",
            useTexture = true,
            additiveBlending = false,
            
            maxParticles = 10000
        }
    }
}
