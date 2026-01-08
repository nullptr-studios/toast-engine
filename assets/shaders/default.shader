{
	"stageFiles": [
		{
			"stage": "vertex",
			"path": "shaders/default.vert"
		},
		{
			"stage": "fragment",
			"path": "shaders/default.frag"
		}
	],
	"parameters": [
	    {
	        "name": "gTexture",
	        "type": "texture",
	        "defaultValue": "images/default.png"
	    },
	    {
	        "name": "gNormalMap",
            "type": "texture",
            "defaultValue": "images/NoNormals.png"
	    },
	    {
	        "name": "gColor",
	        "type": "color"
	    }
	]
	
}