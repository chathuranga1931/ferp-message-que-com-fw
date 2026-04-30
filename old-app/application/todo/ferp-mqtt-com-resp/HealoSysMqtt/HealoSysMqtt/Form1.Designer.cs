namespace HealoSysMqtt
{
    partial class Form1
    {
        /// <summary>
        ///  Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        ///  Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        ///  Required method for Designer support - do not modify
        ///  the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            label1 = new Label();
            txtMqttUrl = new TextBox();
            BtnConnect = new Button();
            openFileDialog1 = new OpenFileDialog();
            txtBrowse = new TextBox();
            tabControl1 = new TabControl();
            tabPage1 = new TabPage();
            lblProgress = new Label();
            progressBarOta = new ProgressBar();
            btnOtaStart = new Button();
            btnBrowse = new Button();
            label2 = new Label();
            tabPage2 = new TabPage();
            btn_Reset = new Button();
            cmbSelectDevice = new ComboBox();
            txtPort = new TextBox();
            lblStatus = new Label();
            rtb_DebugLogs = new RichTextBox();
            tabControl1.SuspendLayout();
            tabPage1.SuspendLayout();
            tabPage2.SuspendLayout();
            SuspendLayout();
            // 
            // label1
            // 
            label1.AutoSize = true;
            label1.Location = new Point(10, 14);
            label1.Name = "label1";
            label1.Size = new Size(84, 15);
            label1.TabIndex = 0;
            label1.Text = "MQTT-URL(IP)";
            // 
            // txtMqttUrl
            // 
            txtMqttUrl.Location = new Point(105, 11);
            txtMqttUrl.Margin = new Padding(3, 2, 3, 2);
            txtMqttUrl.Name = "txtMqttUrl";
            txtMqttUrl.Size = new Size(384, 23);
            txtMqttUrl.TabIndex = 1;
            txtMqttUrl.Text = "144.24.156.245";
            // 
            // BtnConnect
            // 
            BtnConnect.Location = new Point(604, 10);
            BtnConnect.Margin = new Padding(3, 2, 3, 2);
            BtnConnect.Name = "BtnConnect";
            BtnConnect.Size = new Size(82, 22);
            BtnConnect.TabIndex = 2;
            BtnConnect.Text = "Connect";
            BtnConnect.UseVisualStyleBackColor = true;
            BtnConnect.Click += BtnConnect_Click;
            // 
            // openFileDialog1
            // 
            openFileDialog1.FileName = "openFileDialog1";
            // 
            // txtBrowse
            // 
            txtBrowse.Location = new Point(99, 14);
            txtBrowse.Margin = new Padding(3, 2, 3, 2);
            txtBrowse.Name = "txtBrowse";
            txtBrowse.Size = new Size(445, 23);
            txtBrowse.TabIndex = 3;
            // 
            // tabControl1
            // 
            tabControl1.Controls.Add(tabPage1);
            tabControl1.Controls.Add(tabPage2);
            tabControl1.Location = new Point(10, 75);
            tabControl1.Margin = new Padding(3, 2, 3, 2);
            tabControl1.Name = "tabControl1";
            tabControl1.SelectedIndex = 0;
            tabControl1.Size = new Size(679, 253);
            tabControl1.TabIndex = 4;
            // 
            // tabPage1
            // 
            tabPage1.Controls.Add(lblProgress);
            tabPage1.Controls.Add(progressBarOta);
            tabPage1.Controls.Add(btnOtaStart);
            tabPage1.Controls.Add(btnBrowse);
            tabPage1.Controls.Add(label2);
            tabPage1.Controls.Add(txtBrowse);
            tabPage1.Location = new Point(4, 24);
            tabPage1.Margin = new Padding(3, 2, 3, 2);
            tabPage1.Name = "tabPage1";
            tabPage1.Padding = new Padding(3, 2, 3, 2);
            tabPage1.Size = new Size(671, 225);
            tabPage1.TabIndex = 0;
            tabPage1.Text = "Ota";
            tabPage1.UseVisualStyleBackColor = true;
            // 
            // lblProgress
            // 
            lblProgress.AutoSize = true;
            lblProgress.Location = new Point(596, 100);
            lblProgress.Name = "lblProgress";
            lblProgress.RightToLeft = RightToLeft.No;
            lblProgress.Size = new Size(38, 15);
            lblProgress.TabIndex = 8;
            lblProgress.Text = "100 %";
            lblProgress.TextAlign = ContentAlignment.MiddleRight;
            // 
            // progressBarOta
            // 
            progressBarOta.Location = new Point(21, 82);
            progressBarOta.Margin = new Padding(3, 2, 3, 2);
            progressBarOta.Name = "progressBarOta";
            progressBarOta.Size = new Size(618, 9);
            progressBarOta.TabIndex = 7;
            // 
            // btnOtaStart
            // 
            btnOtaStart.Location = new Point(556, 49);
            btnOtaStart.Margin = new Padding(3, 2, 3, 2);
            btnOtaStart.Name = "btnOtaStart";
            btnOtaStart.Size = new Size(82, 22);
            btnOtaStart.TabIndex = 6;
            btnOtaStart.Text = "Start Ota";
            btnOtaStart.UseVisualStyleBackColor = true;
            btnOtaStart.Click += btnOtaStart_Click;
            // 
            // btnBrowse
            // 
            btnBrowse.Location = new Point(556, 14);
            btnBrowse.Margin = new Padding(3, 2, 3, 2);
            btnBrowse.Name = "btnBrowse";
            btnBrowse.Size = new Size(82, 22);
            btnBrowse.TabIndex = 5;
            btnBrowse.Text = "Browse";
            btnBrowse.UseVisualStyleBackColor = true;
            btnBrowse.Click += btnBrowse_Click;
            // 
            // label2
            // 
            label2.AutoSize = true;
            label2.Location = new Point(5, 16);
            label2.Name = "label2";
            label2.Size = new Size(79, 15);
            label2.TabIndex = 4;
            label2.Text = "Select Bin File";
            // 
            // tabPage2
            // 
            tabPage2.Controls.Add(btn_Reset);
            tabPage2.Location = new Point(4, 24);
            tabPage2.Margin = new Padding(3, 2, 3, 2);
            tabPage2.Name = "tabPage2";
            tabPage2.Padding = new Padding(3, 2, 3, 2);
            tabPage2.Size = new Size(671, 225);
            tabPage2.TabIndex = 1;
            tabPage2.Text = "Commands";
            tabPage2.UseVisualStyleBackColor = true;
            // 
            // btn_Reset
            // 
            btn_Reset.Location = new Point(16, 17);
            btn_Reset.Name = "btn_Reset";
            btn_Reset.Size = new Size(75, 23);
            btn_Reset.TabIndex = 0;
            btn_Reset.Text = "Reset";
            btn_Reset.UseVisualStyleBackColor = true;
            btn_Reset.Click += button1_Click;
            // 
            // cmbSelectDevice
            // 
            cmbSelectDevice.FormattingEnabled = true;
            cmbSelectDevice.Items.AddRange(new object[] { "ABCDEF123456", "48E729331048" });
            cmbSelectDevice.Location = new Point(10, 42);
            cmbSelectDevice.Margin = new Padding(3, 2, 3, 2);
            cmbSelectDevice.Name = "cmbSelectDevice";
            cmbSelectDevice.Size = new Size(133, 23);
            cmbSelectDevice.TabIndex = 9;
            cmbSelectDevice.Text = "Select Device ID";
            // 
            // txtPort
            // 
            txtPort.Location = new Point(494, 11);
            txtPort.Margin = new Padding(3, 2, 3, 2);
            txtPort.Name = "txtPort";
            txtPort.Size = new Size(96, 23);
            txtPort.TabIndex = 5;
            txtPort.Text = "1883";
            // 
            // lblStatus
            // 
            lblStatus.AutoSize = true;
            lblStatus.Location = new Point(599, 34);
            lblStatus.Name = "lblStatus";
            lblStatus.Size = new Size(79, 15);
            lblStatus.TabIndex = 6;
            lblStatus.Text = "Disconnected";
            // 
            // rtb_DebugLogs
            // 
            rtb_DebugLogs.Font = new Font("Consolas", 9.75F, FontStyle.Regular, GraphicsUnit.Point, 0);
            rtb_DebugLogs.Location = new Point(10, 333);
            rtb_DebugLogs.Name = "rtb_DebugLogs";
            rtb_DebugLogs.ReadOnly = true;
            rtb_DebugLogs.Size = new Size(675, 291);
            rtb_DebugLogs.TabIndex = 10;
            rtb_DebugLogs.Text = "";
            // 
            // Form1
            // 
            AutoScaleDimensions = new SizeF(7F, 15F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(700, 636);
            Controls.Add(rtb_DebugLogs);
            Controls.Add(cmbSelectDevice);
            Controls.Add(lblStatus);
            Controls.Add(txtPort);
            Controls.Add(tabControl1);
            Controls.Add(BtnConnect);
            Controls.Add(txtMqttUrl);
            Controls.Add(label1);
            Margin = new Padding(3, 2, 3, 2);
            Name = "Form1";
            Text = "Form1";
            tabControl1.ResumeLayout(false);
            tabPage1.ResumeLayout(false);
            tabPage1.PerformLayout();
            tabPage2.ResumeLayout(false);
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private Label label1;
        private TextBox txtMqttUrl;
        private Button BtnConnect;
        private OpenFileDialog openFileDialog1;
        private TextBox txtBrowse;
        private TabControl tabControl1;
        private TabPage tabPage1;
        private Label label2;
        private TabPage tabPage2;
        private Label lblProgress;
        private ProgressBar progressBarOta;
        private Button btnOtaStart;
        private Button btnBrowse;
        private TextBox txtPort;
        private Label lblStatus;
        private ComboBox cmbSelectDevice;
        private Button btn_Reset;
        private RichTextBox rtb_DebugLogs;
    }
}
