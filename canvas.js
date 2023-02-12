var first_layer = document.getElementById('canvas');
if (!first_layer) {
  throw new Error('Canvas element not found');
}
console.log(first_layer.id);
if (typeof first_layer.id !== 'string') {
  throw new Error('Canvas element has invalid id');
}
if (typeof first_layer.getContext !== 'function') {
  throw new Error('getContext method is not available');
}
//var MyC = first_layer.getContext('2d');
/*var img;
img = new Image();
img.src = 'image.bmp';
MyC.drawImage(img, 0, 0)
*/
