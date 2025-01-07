
var set_fps = 1000 / 30;

window.onload = function() {
		var first_layer = document.getElementById('canvas');
		var MyC = first_layer.getContext('2d');
		var img = new Image();
		img.src = 'image.png';


	   (function (window) {
	   
		function gameLoop() {

			/* This is used to clear the canvas
			and it's apparently faster than clear rect
			*/
			first_layer.height = first_layer.height;
			MyC.drawImage(img, 0, 0);


	    }
		window.setInterval(gameLoop, set_fps); // 30fps
				
	} (window));

};  
