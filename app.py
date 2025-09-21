from flask import Flask, request, jsonify
import numpy as np
import tensorflow as tf
import joblib
import os
from flask_cors import CORS

app = Flask(__name__)
CORS(app)  # Enable CORS for all routes

# Global variables for models and scalers
crop_model = None
soil_model = None
crop_scaler = None
soil_scaler = None

# Crop labels (from your training data)
CROP_LABELS = [
    'apple', 'banana', 'blackgram', 'chickpea', 'coconut', 'coffee',
    'cotton', 'grapes', 'jute', 'kidneybeans', 'lentil', 'maize',
    'mango', 'mothbeans', 'mungbean', 'muskmelon', 'orange', 'papaya',
    'pigeonpeas', 'pomegranate', 'rice', 'watermelon'
]

SOIL_CLASSES = ['Low', 'Mild', 'High']

def load_models():
    """Load TensorFlow Lite models and scalers"""
    global crop_model, soil_model, crop_scaler, soil_scaler
    
    try:
        # Load TFLite models
        crop_model = tf.lite.Interpreter(model_path='crop_model.tflite')
        crop_model.allocate_tensors()
        
        soil_model = tf.lite.Interpreter(model_path='soil_model.tflite')
        soil_model.allocate_tensors()
        
        # Load scalers (you'll need to save these during training)
        crop_scaler = joblib.load('crop_scaler.pkl')
        soil_scaler = joblib.load('soil_scaler.pkl')
        
        print("? Models and scalers loaded successfully")
        return True
    except Exception as e:
        print(f"? Error loading models: {e}")
        return False

def predict_crop(input_data):
    """Make crop prediction using TFLite model"""
    try:
        # Get input/output details
        input_details = crop_model.get_input_details()
        output_details = crop_model.get_output_details()
        
        # Prepare input data
        input_array = np.array(input_data, dtype=np.float32).reshape(1, -1)
        scaled_input = crop_scaler.transform(input_array)
        
        # Set input tensor
        crop_model.set_tensor(input_details[0]['index'], scaled_input.astype(np.float32))
        
        # Run inference
        crop_model.invoke()
        
        # Get output
        output = crop_model.get_tensor(output_details[0]['index'])
        predicted_class = np.argmax(output[0])
        confidence = float(np.max(output[0]))
        
        return {
            'predicted_crop': CROP_LABELS[predicted_class],
            'confidence': confidence,
            'class_index': int(predicted_class)
        }
    except Exception as e:
        return {'error': str(e)}

def predict_soil(input_data):
    """Make soil fertility prediction using TFLite model"""
    try:
        # Get input/output details
        input_details = soil_model.get_input_details()
        output_details = soil_model.get_output_details()
        
        # Prepare input data
        input_array = np.array(input_data, dtype=np.float32).reshape(1, -1)
        scaled_input = soil_scaler.transform(input_array)
        
        # Set input tensor
        soil_model.set_tensor(input_details[0]['index'], scaled_input.astype(np.float32))
        
        # Run inference
        soil_model.invoke()
        
        # Get output
        output = soil_model.get_tensor(output_details[0]['index'])
        predicted_class = np.argmax(output[0])
        confidence = float(np.max(output[0]))
        
        return {
            'predicted_fertility': SOIL_CLASSES[predicted_class],
            'confidence': confidence,
            'class_index': int(predicted_class)
        }
    except Exception as e:
        return {'error': str(e)}

@app.route('/', methods=['GET'])
def home():
    """Health check endpoint"""
    return jsonify({
        'status': 'API is running',
        'endpoints': {
            'crop_prediction': '/predict/crop',
            'soil_prediction': '/predict/soil',
            'combined_prediction': '/predict/combined'
        }
    })

@app.route('/predict/crop', methods=['POST'])
def predict_crop_endpoint():
    """
    Predict crop recommendation
    Expected input: {
        "N": 90, "P": 42, "K": 43,
        "temperature": 20.9, "humidity": 82.0,
        "ph": 6.5, "rainfall": 202.9
    }
    """
    try:
        data = request.get_json()
        
        # Validate input
        required_fields = ['N', 'P', 'K', 'temperature', 'humidity', 'ph', 'rainfall']
        if not all(field in data for field in required_fields):
            return jsonify({'error': f'Missing required fields: {required_fields}'}), 400
        
        # Extract features in correct order
        input_features = [
            data['N'], data['P'], data['K'],
            data['temperature'], data['humidity'],
            data['ph'], data['rainfall']
        ]
        
        # Make prediction
        result = predict_crop(input_features)
        
        if 'error' in result:
            return jsonify(result), 500
        
        return jsonify({
            'success': True,
            'prediction': result,
            'input_data': data
        })
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/predict/soil', methods=['POST'])
def predict_soil_endpoint():
    """
    Predict soil fertility
    Expected input: {
        "N": 90, "P": 42, "K": 43, "pH": 6.5
    }
    """
    try:
        data = request.get_json()
        
        # Validate input
        required_fields = ['N', 'P', 'K', 'pH']
        if not all(field in data for field in required_fields):
            return jsonify({'error': f'Missing required fields: {required_fields}'}), 400
        
        # Extract features in correct order
        input_features = [data['N'], data['P'], data['K'], data['pH']]
        
        # Make prediction
        result = predict_soil(input_features)
        
        if 'error' in result:
            return jsonify(result), 500
        
        return jsonify({
            'success': True,
            'prediction': result,
            'input_data': data
        })
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/predict/combined', methods=['POST'])
def predict_combined_endpoint():
    """
    Get both crop and soil predictions
    Expected input: {
        "N": 90, "P": 42, "K": 43, "pH": 6.5,
        "temperature": 20.9, "humidity": 82.0, "rainfall": 202.9
    }
    """
    try:
        data = request.get_json()
        
        # Validate input for both models
        crop_fields = ['N', 'P', 'K', 'temperature', 'humidity', 'ph', 'rainfall']
        soil_fields = ['N', 'P', 'K', 'pH']
        
        # Use pH for both (soil model uses pH, crop model uses ph)
        if 'pH' in data and 'ph' not in data:
            data['ph'] = data['pH']
        elif 'ph' in data and 'pH' not in data:
            data['pH'] = data['ph']
        
        if not all(field in data for field in crop_fields):
            return jsonify({'error': f'Missing crop fields: {crop_fields}'}), 400
        
        if not all(field in data for field in soil_fields):
            return jsonify({'error': f'Missing soil fields: {soil_fields}'}), 400
        
        # Make crop prediction
        crop_features = [
            data['N'], data['P'], data['K'],
            data['temperature'], data['humidity'],
            data['ph'], data['rainfall']
        ]
        crop_result = predict_crop(crop_features)
        
        # Make soil prediction
        soil_features = [data['N'], data['P'], data['K'], data['pH']]
        soil_result = predict_soil(soil_features)
        
        return jsonify({
            'success': True,
            'crop_prediction': crop_result,
            'soil_prediction': soil_result,
            'input_data': data
        })
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/sensor-data', methods=['POST'])
def process_sensor_data():
    """
    Process Arduino sensor data and return predictions
    Expected input from Arduino: {
        "soilHumidity": 45.2,
        "soilTemperature": 25.3,
        "soilConductivity": 1200,
        "soilPH": 6.5,
        "nitrogen": 90,
        "phosphorus": 42,
        "potassium": 43,
        "temperature": 20.9,  // air temperature
        "humidity": 82.0,     // air humidity
        "rainfall": 202.9
    }
    """
    try:
        data = request.get_json()
        
        # Extract required values (convert pH from sensor format if needed)
        ph_value = data.get('soilPH', 6.5)
        if ph_value > 100:  # If pH is in sensor format (e.g., 65 for 6.5)
            ph_value = ph_value / 10.0
        
        # Prepare data for both models
        prediction_data = {
            'N': data.get('nitrogen', 0),
            'P': data.get('phosphorus', 0),
            'K': data.get('potassium', 0),
            'pH': ph_value,
            'ph': ph_value,
            'temperature': data.get('temperature', 25.0),
            'humidity': data.get('humidity', 60.0),
            'rainfall': data.get('rainfall', 100.0)
        }
        
        # Make predictions
        crop_features = [
            prediction_data['N'], prediction_data['P'], prediction_data['K'],
            prediction_data['temperature'], prediction_data['humidity'],
            prediction_data['ph'], prediction_data['rainfall']
        ]
        crop_result = predict_crop(crop_features)
        
        soil_features = [prediction_data['N'], prediction_data['P'], prediction_data['K'], prediction_data['pH']]
        soil_result = predict_soil(soil_features)
        
        return jsonify({
            'success': True,
            'sensor_data': data,
            'recommendations': {
                'crop': crop_result,
                'soil_fertility': soil_result
            },
            'processed_values': prediction_data
        })
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    # Load models on startup
    if not load_models():
        print("??  Running without models - predictions will fail")
    
    # Get port from environment variable (for deployment)
    port = int(os.environ.get('PORT', 5000))
    app.run(host='0.0.0.0', port=port, debug=False)