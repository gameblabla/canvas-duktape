var first_layer = document.getElementById('canvas');
var MyC = first_layer.getContext('2d');
var img;
img = new Image();
img.src = 'image.bmp';
MyC.drawImage(img, 0, 0)
