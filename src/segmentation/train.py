import os
import time
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, random_split, Dataset
from torchvision import transforms
import numpy as np
from PIL import Image
import matplotlib.pyplot as plt

# Para métricas adicionales
from sklearn.metrics import classification_report, confusion_matrix, roc_auc_score, roc_curve

# ==============================================================================
# DEFINICIÓN DEL MODELO Y DATASET
# ==============================================================================

class BorderDetectionCNN(nn.Module):
    def __init__(self, num_classes=2):
        super(BorderDetectionCNN, self).__init__()
        
        # Capas convolucionales
        self.conv1 = nn.Conv2d(3, 32, kernel_size=3, padding=1)
        self.conv2 = nn.Conv2d(32, 64, kernel_size=3, padding=1)
        self.conv3 = nn.Conv2d(64, 128, kernel_size=3, padding=1)
        self.conv4 = nn.Conv2d(128, 256, kernel_size=3, padding=1)
        
        # BatchNorm
        self.bn1 = nn.BatchNorm2d(32)
        self.bn2 = nn.BatchNorm2d(64)
        self.bn3 = nn.BatchNorm2d(128)
        self.bn4 = nn.BatchNorm2d(256)
        
        # Dropout para regularización
        self.dropout = nn.Dropout(0.2)
        
        # MaxPool 2x2
        self.pool = nn.MaxPool2d(kernel_size=2, stride=2)
        
        # Clasificador final
        # Para imagen de entrada 224x224, cada pool reduce a la mitad:
        # 224 -> 112 -> 56 -> 28 -> 14
        self.fc1 = nn.Linear(256 * 14 * 14, 512)
        self.fc2 = nn.Linear(512, num_classes)
        
        self.relu = nn.ReLU()

    def forward(self, x):
        x = self.conv1(x)
        x = self.bn1(x)
        x = self.relu(x)
        x = self.pool(x)

        x = self.conv2(x)
        x = self.bn2(x)
        x = self.relu(x)
        x = self.pool(x)

        x = self.conv3(x)
        x = self.bn3(x)
        x = self.relu(x)
        x = self.pool(x)

        x = self.conv4(x)
        x = self.bn4(x)
        x = self.relu(x)
        x = self.pool(x)

        x = x.view(x.size(0), -1)  # Flatten

        x = self.fc1(x)
        x = self.relu(x)
        x = self.dropout(x)
        x = self.fc2(x)
        return x

class BorderDataset(Dataset):
    def __init__(self, root_dir, transform=None):
        self.root_dir = root_dir
        self.transform = transform
        self.samples = []
        self.rotation_angles = [90, 180, 270]  # Ángulos para rotaciones adicionales
        
        borders_dir = os.path.join(root_dir, "borders")
        for img_name in os.listdir(borders_dir):
            if img_name.lower().endswith(('.png', '.jpg', '.jpeg', '.tiff', '.bmp')):
                img_path = os.path.join(borders_dir, img_name)
                # Imagen original sin rotación
                self.samples.append((img_path, 0))
                # Imágenes rotadas
                for angle in self.rotation_angles:
                    self.samples.append((img_path, 0, angle))
        
        no_borders_dir = os.path.join(root_dir, "no_borders")
        for img_name in os.listdir(no_borders_dir):
            if img_name.lower().endswith(('.png', '.jpg', '.jpeg', '.tiff', '.bmp')):
                img_path = os.path.join(no_borders_dir, img_name)
                self.samples.append((img_path, 1))
                # Si se desea aplicar augmentation a imágenes sin bordes, se puede descomentar:
                # for angle in self.rotation_angles:
                #     self.samples.append((img_path, 1, angle))
        
    def __len__(self):
        return len(self.samples)
    
    def __getitem__(self, idx):
        if len(self.samples[idx]) == 2:
            # Imagen sin rotación
            img_path, label = self.samples[idx]
            image = Image.open(img_path).convert('RGB')
        else:
            # Imagen con rotación
            img_path, label, angle = self.samples[idx]
            image = Image.open(img_path).convert('RGB')
            image = image.rotate(angle)
        
        if self.transform:
            image = self.transform(image)
        
        return image, label

def check_data_directory(data_dir):
    """Verifica que existan las carpetas necesarias con las imágenes"""
    if not os.path.exists(data_dir):
        raise FileNotFoundError(f"El directorio {data_dir} no existe")
    
    borders_dir = os.path.join(data_dir, "borders")
    no_borders_dir = os.path.join(data_dir, "no_borders")
    
    if not os.path.exists(borders_dir):
        raise FileNotFoundError(f"No se encuentra la carpeta 'borders' en {data_dir}")
    if not os.path.exists(no_borders_dir):
        raise FileNotFoundError(f"No se encuentra la carpeta 'no_borders' en {data_dir}")
    
    # Verificar que hay imágenes en las carpetas
    valid_extensions = ('.jpg', '.jpeg', '.png', '.ppm', '.bmp', '.pgm', '.tif', '.tiff', '.webp')
    borders_images = [f for f in os.listdir(borders_dir) if f.lower().endswith(valid_extensions)]
    no_borders_images = [f for f in os.listdir(no_borders_dir) if f.lower().endswith(valid_extensions)]
    
    if not borders_images:
        raise FileNotFoundError(f"No se encontraron imágenes válidas en {borders_dir}")
    if not no_borders_images:
        raise FileNotFoundError(f"No se encontraron imágenes válidas en {no_borders_dir}")
    
    print(f"Encontradas {len(borders_images)} imágenes con bordes")
    print(f"Después del data augmentation: {len(borders_images) * 4} imágenes con bordes")
    print(f"Encontradas {len(no_borders_images)} imágenes sin bordes")

# ==============================================================================
# FUNCIÓN PRINCIPAL DE ENTRENAMIENTO Y EVALUACIÓN
# ==============================================================================

def main():
    # ==============================================================================
    # CONFIGURACIÓN BÁSICA
    # ==============================================================================
    DATASET_DIR = "dataset"
    DATA_DIR = os.path.abspath(DATASET_DIR)
    BATCH_SIZE = 32
    EPOCHS = 10
    LEARNING_RATE = 1e-3

    # Verificar la estructura del directorio y las imágenes
    try:
        check_data_directory(DATA_DIR)
    except FileNotFoundError as e:
        print(f"Error al cargar los datos: {str(e)}")
        print("\nAsegúrate de que tu estructura de directorios sea así:")
        print(f"{DATASET_DIR}/")
        print("├── borders/")
        print("│   ├── imagen1.jpg")
        print("│   └── ...")
        print("└── no_borders/")
        print("    ├── imagen1.jpg")
        print("    └── ...")
        return

    # Dispositivo: GPU si está disponible, sino CPU
    DEVICE = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"Usando dispositivo: {DEVICE}")

    # ==============================================================================
    # TRANSFORMS Y DATASET
    # ==============================================================================
    data_transforms = transforms.Compose([
        transforms.Resize((224, 224)),
        transforms.ToTensor(),
        transforms.Normalize(mean=[0.485, 0.456, 0.406], 
                             std=[0.229, 0.224, 0.225])
    ])

    # Usar nuestro dataset personalizado
    full_dataset = BorderDataset(DATA_DIR, transform=data_transforms)
    
    # Dividir el dataset en Train, Validación y Test (por ejemplo, 70%, 15%, 15%)
    total_size = len(full_dataset)
    train_size = int(0.7 * total_size)
    val_size = int(0.15 * total_size)
    test_size = total_size - train_size - val_size
    train_dataset, val_dataset, test_dataset = random_split(full_dataset, [train_size, val_size, test_size])

    # Ajustar num_workers a 0 para evitar bloqueos (sobre todo en Windows)
    train_loader = DataLoader(train_dataset, batch_size=BATCH_SIZE, shuffle=True, num_workers=0)
    val_loader = DataLoader(val_dataset, batch_size=BATCH_SIZE, shuffle=False, num_workers=0)
    test_loader = DataLoader(test_dataset, batch_size=BATCH_SIZE, shuffle=False, num_workers=0)

    print(f"Total de imágenes (incluyendo augmentation): {total_size}")
    print(f"Imágenes de entrenamiento: {train_size}")
    print(f"Imágenes de validación: {val_size}")
    print(f"Imágenes de test: {test_size}")

    # Instanciar el modelo y moverlo a GPU/CPU
    model = BorderDetectionCNN(num_classes=2).to(DEVICE)

    # Definir función de pérdida y optimizador
    criterion = nn.CrossEntropyLoss()
    optimizer = optim.AdamW(model.parameters(), lr=LEARNING_RATE)
    
    # Scheduler para ajuste dinámico del LR (opcional)
    scheduler = optim.lr_scheduler.ReduceLROnPlateau(optimizer, 'min', patience=2)

    # ==============================================================================
    # ENTRENAMIENTO
    # ==============================================================================

    # Inicializar listas para graficar
    train_losses, val_losses, test_losses = [], [], []
    train_accuracies, val_accuracies, test_accuracies = [], [], []

    def train_one_epoch(epoch):
        model.train()
        running_loss = 0.0
        correct = 0
        total = 0
        
        for i, (inputs, labels) in enumerate(train_loader):
            inputs, labels = inputs.to(DEVICE), labels.to(DEVICE)
            
            optimizer.zero_grad()
            outputs = model(inputs)
            loss = criterion(outputs, labels)
            loss.backward()
            optimizer.step()
            
            running_loss += loss.item()
            _, predicted = torch.max(outputs.data, 1)
            total += labels.size(0)
            correct += (predicted == labels).sum().item()
            
            if (i + 1) % 10 == 0:
                print(f'  Época [{epoch+1}/{EPOCHS}], Batch [{i+1}/{len(train_loader)}], '
                      f'Loss: {loss.item():.4f}')
        epoch_loss = running_loss / len(train_loader)
        epoch_acc = 100.0 * correct / total
        return epoch_loss, epoch_acc

    def validate_one_epoch():
        model.eval()
        running_loss = 0.0
        correct = 0
        total = 0
        
        with torch.no_grad():
            for inputs, labels in val_loader:
                inputs, labels = inputs.to(DEVICE), labels.to(DEVICE)
                outputs = model(inputs)
                loss = criterion(outputs, labels)
                
                running_loss += loss.item()
                _, predicted = torch.max(outputs.data, 1)
                total += labels.size(0)
                correct += (predicted == labels).sum().item()
        val_loss = running_loss / len(val_loader)
        val_acc = 100.0 * correct / total
        return val_loss, val_acc

    def test_one_epoch():
        model.eval()
        running_loss = 0.0
        correct = 0
        total = 0
        all_labels = []
        all_preds = []
        all_probs = []  # Probabilidades para la clase 1 (útil para la curva ROC)
        with torch.no_grad():
            for inputs, labels in test_loader:
                inputs, labels = inputs.to(DEVICE), labels.to(DEVICE)
                outputs = model(inputs)
                loss = criterion(outputs, labels)
                
                running_loss += loss.item()
                _, predicted = torch.max(outputs.data, 1)
                total += labels.size(0)
                correct += (predicted == labels).sum().item()
                
                all_labels.extend(labels.cpu().numpy())
                all_preds.extend(predicted.cpu().numpy())
                # Calcular probabilidades con softmax y quedarnos con la probabilidad de la clase 1
                probs = torch.softmax(outputs, dim=1)
                all_probs.extend(probs[:, 1].cpu().numpy())
                
        avg_loss = running_loss / len(test_loader)
        accuracy = 100.0 * correct / total
        return avg_loss, accuracy, all_labels, all_preds, all_probs

    best_val_loss = float('inf')
    start_time = time.time()
    
    for epoch in range(EPOCHS):
        print(f"==> Época {epoch+1}/{EPOCHS} <==")
        train_loss, train_acc = train_one_epoch(epoch)
        val_loss, val_acc = validate_one_epoch()
        test_loss, test_acc, _, _, _ = test_one_epoch()
        
        # Guardar resultados
        train_losses.append(train_loss)
        val_losses.append(val_loss)
        test_losses.append(test_loss)
        train_accuracies.append(train_acc)
        val_accuracies.append(val_acc)
        test_accuracies.append(test_acc)

        # Actualizar learning rate con el scheduler (si se usa)
        scheduler.step(val_loss)
        
        # Guardar el mejor modelo (según la pérdida de validación)
        if val_loss < best_val_loss:
            best_val_loss = val_loss
            torch.save(model.state_dict(), "model_new.pth")

        # Mostrar métricas de la época
        print(f"Train Loss: {train_loss:.4f} | Train Acc: {train_acc:.2f}%")
        print(f"Val   Loss: {val_loss:.4f} | Val   Acc: {val_acc:.2f}%")
        print(f"Test  Loss: {test_loss:.4f} | Test  Acc: {test_acc:.2f}%")
        print("-" * 60)

    total_time = time.time() - start_time
    print(f"Entrenamiento finalizado en {total_time:.2f} segundos.")
    print(f"Mejor modelo guardado como 'model.pth'")
    
    # ==============================================================================
    # EVALUACIÓN FINAL SOBRE EL CONJUNTO DE TEST
    # ==============================================================================
    # Cargar el mejor modelo guardado
    model.load_state_dict(torch.load("model.pth"))
    final_test_loss, final_test_acc, all_labels, all_preds, all_probs = test_one_epoch()
    print(f"Final Test Loss: {final_test_loss:.4f} | Final Test Acc: {final_test_acc:.2f}%")
    
    # Imprimir Classification Report y Confusion Matrix
    print("\nClassification Report:")
    print(classification_report(all_labels, all_preds, target_names=["borders", "no_borders"]))
    
    cm = confusion_matrix(all_labels, all_preds)
    print("Confusion Matrix:")
    print(cm)
    
    # Graficar la matriz de confusión
    plt.figure(figsize=(6, 5))
    plt.imshow(cm, interpolation='nearest', cmap=plt.cm.Blues)
    plt.title("Matriz de Confusión")
    plt.colorbar()
    tick_marks = np.arange(2)
    plt.xticks(tick_marks, ["borders", "no_borders"], rotation=45)
    plt.yticks(tick_marks, ["borders", "no_borders"])
    thresh = cm.max() / 2.
    for i in range(cm.shape[0]):
        for j in range(cm.shape[1]):
            plt.text(j, i, format(cm[i, j], 'd'),
                     horizontalalignment="center",
                     color="white" if cm[i, j] > thresh else "black")
    plt.tight_layout()
    plt.ylabel('Etiqueta verdadera')
    plt.xlabel('Etiqueta predicha')
    plt.show()
    
    # Graficar la curva ROC y calcular el AUC (para la clase "no_borders")
    try:
        auc = roc_auc_score(all_labels, all_probs)
        fpr, tpr, thresholds = roc_curve(all_labels, all_probs)
        plt.figure(figsize=(6, 5))
        plt.plot(fpr, tpr, label=f"Curva ROC (AUC = {auc:.2f})")
        plt.plot([0, 1], [0, 1], 'k--')
        plt.xlabel("False Positive Rate")
        plt.ylabel("True Positive Rate")
        plt.title("Curva ROC")
        plt.legend(loc="lower right")
        plt.show()
    except Exception as e:
        print("No se pudo calcular la curva ROC:", e)
    
    # ==============================================================================
    # GRAFICAS DE CURVAS DE ENTRENAMIENTO, VALIDACIÓN Y TEST
    # ==============================================================================
    plt.figure(figsize=(12, 6))
    plt.plot(train_losses, label='Train Loss')
    plt.plot(val_losses, label='Val Loss')
    plt.plot(test_losses, label='Test Loss')
    plt.title('Curvas de Pérdida')
    plt.xlabel('Épocas')
    plt.ylabel('Loss')
    plt.legend()
    plt.show()

    plt.figure(figsize=(12,6))
    plt.plot(train_accuracies, label='Train Acc')
    plt.plot(val_accuracies, label='Val Acc')
    plt.plot(test_accuracies, label='Test Acc')
    plt.title('Curvas de Accuracy')
    plt.xlabel('Épocas')
    plt.ylabel('Accuracy (%)')
    plt.legend()
    plt.show()

if __name__ == "__main__":
    main()
