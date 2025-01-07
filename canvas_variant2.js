var first_layer = document.getElementById('canvas');

var MyC = first_layer.getContext('2d');
var img;
img = new Image();

img.addEventListener("load", function() {
  MyC.drawImage(img, 0, 0);
});

img.src = 'image.png';